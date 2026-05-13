/* main.c - Application main entry point */

/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/hogp.h>
#include <dk_buttons_and_leds.h>

#include <zephyr/settings/settings.h>

/**
 * Switch between boot protocol and report protocol mode.
 */
#define KEY_BOOTMODE_MASK DK_BTN2_MSK
/**
 * Switch CAPSLOCK state.
 *
 * @note
 * For simplicity of the code it works only in boot mode.
 */
#define KEY_CAPSLOCK_MASK DK_BTN1_MSK
/**
 * Switch CAPSLOCK state with response
 *
 * Write CAPSLOCK with response.
 * Just for testing purposes.
 * The result should be the same like usine @ref KEY_CAPSLOCK_MASK
 */
#define KEY_CAPSLOCK_RSP_MASK DK_BTN3_MSK

/* Key used to accept or reject passkey value */
#define KEY_PAIRING_ACCEPT DK_BTN1_MSK
#define KEY_PAIRING_REJECT DK_BTN2_MSK

/** Key used to activate additional button functions.
 *  Currently only used for SCI mode selection, but can be extended to other features
 *  in the future.
 */
#define KEY_ADDITIONAL_FUNCTIONS_MASK DK_BTN4_MSK

#if defined(CONFIG_BT_HOGP_SCI)

/* Key used to set next SCI mode */
#define KEY_ADDITIONAL_FUNCTIONS_SCI_MODE_NEXT	DK_BTN2_MSK

BUILD_ASSERT(CONFIG_CENTRAL_HIDS_SCI_SUBRATE_MAX * (1 + CONFIG_CENTRAL_HIDS_SCI_MAX_LATENCY) <=
		     500,
	     "Core connSubrate*(latency+1) must be <= 500");
BUILD_ASSERT(CONFIG_CENTRAL_HIDS_SCI_CONTINUATION_NUM < CONFIG_CENTRAL_HIDS_SCI_SUBRATE_MAX,
	     "continuation number must be less than subrate maximum");
BUILD_ASSERT(CONFIG_CENTRAL_HIDS_SCI_SUPERVISION_TIMEOUT_10MS * 10000ULL >
		     (uint64_t)(1 + CONFIG_CENTRAL_HIDS_SCI_MAX_LATENCY) *
			     CONFIG_CENTRAL_HIDS_SCI_SUBRATE_MAX *
			     CONFIG_CENTRAL_HIDS_SCI_INTERVAL_MAX_125US * 250,
	     "supervision timeout must exceed 2*(1+latency)*subrate*interval");

#define SCI_MODE_CHANGE_TIMEOUT_MS 10000

#endif

/**
 * Entrance into continuous receive mode: after every
 * CONT_REPORT_RX_ENTRANCE_SAMPLE_REPORTS notifications we measure how long that batch took;
 * if it is shorter than CONT_REPORT_RX_ENTRANCE_MAX_SPAN_CONN_INTERVALS connection intervals,
 * we enable continuous mode.
 */
#define CONT_REPORT_RX_ENTRANCE_SAMPLE_REPORTS 10
#define CONT_REPORT_RX_ENTRANCE_MAX_SPAN_CONN_INTERVALS 50

#define CONTINUOUS_REPORT_RECEIVING_ENTRANCE_THRESHOLD_US(SLOT) \
	((SLOT)->cont_report_rx_interval_us * CONT_REPORT_RX_ENTRANCE_MAX_SPAN_CONN_INTERVALS)

/**
 * Exit from continuous receive mode: if no report is received for
 * CONTINUOUS_REPORT_RECEIVING_EXIT_THRESHOLD_US microseconds,
 * continuous rx mode will be automatically disabled.
 */
#define CONTINUOUS_REPORT_RECEIVING_EXIT_THRESHOLD_US 500000

/** The statistics for the continuous report receiving mode will be printed every 500 reports. */
#define CONTINUOUS_REPORT_RECEIVING_PRINT_INFO_RATE 2000

/**
 * Reports are considered to be within the expected window if the difference between
 * the report interval and the connection interval is less than this value.
 * The sample tracks how many reports are received outside this expected time window,
 * both above and below the threshold.
 */
#define CONTINUOUS_REPORT_RECEIVING_WINDOW_DELTA_US 200

/** Stack for stats print thread (several printk lines per batch). */
#define CONT_REPORT_RX_STATS_PRINT_STACK_SIZE 1024
/*
 * One cooperative priority level below the Bluetooth RX work queue priority
 * (K_PRIO_COOP(CONFIG_BT_RX_PRIO); see Zephyr hci_core.c / CONFIG_BT_RX_PRIO).
 * This is to ensure the notify path is favored over the stats print path.
 */
#define CONT_REPORT_RX_STATS_PRINT_PRIO \
	K_PRIO_COOP(MIN(CONFIG_BT_RX_PRIO + 1, CONFIG_NUM_COOP_PRIORITIES - 1))

/* Small bounded queue between notify path and the stats print thread. */
#define CONT_REPORT_RX_STATS_MSGQ_MAX_MESSAGES 4U

#define PERIPHERAL_SLOT_COUNT CONFIG_SAMPLE_BT_CENTRAL_HIDS_PERIPHERAL_COUNT

#if PERIPHERAL_SLOT_COUNT > 1
#define PRINT_PERIPH_INDEX_STR(idx) printk("Peripheral %zu: ", (idx))
#define PERIPH_SLOT_INDEX(slot) ((size_t)((slot) - (slots)))
#else
#define PRINT_PERIPH_INDEX_STR(idx)
#define PERIPH_SLOT_INDEX(slot) 0
#endif

struct peripheral_slot {
	struct bt_conn *conn;
	struct bt_hogp hogp;
	struct k_work hids_ready_work;
	struct k_timer cont_report_rx_timer;
	atomic_t cont_report_rx_on;
	uint32_t cont_report_rx_previous_report_cycles;
	uint32_t cont_report_rx_interval_us;
	uint32_t cont_report_rx_max_interval_us;
	uint32_t cont_report_rx_min_interval_us;
	uint32_t cont_report_rx_above_window_count;
	uint32_t cont_report_rx_below_window_count;
	uint32_t hogp_notify_batch_start_cycles;
	uint32_t hogp_notify_cnt;
#if defined(CONFIG_BT_HOGP_SCI)
	enum bt_hids_sci_mode_value sci_mode_requested;
	struct k_work_delayable sci_mode_timeout;
#endif
};

static struct peripheral_slot slots[PERIPHERAL_SLOT_COUNT];
static struct bt_conn *auth_conn;
static uint8_t capslock_state;
static bool btn_additional_functions_active;

struct cont_report_rx_data {
	size_t peripheral_idx;
	int notify_cnt;
	uint32_t report_interval_us;
	uint32_t total_time_us;
	uint32_t max_interval_us;
	uint32_t min_interval_us;
	uint32_t above_window_count;
	uint32_t below_window_count;
};


static void hids_on_ready(struct k_work *work);

#if defined(CONFIG_BT_HOGP_SCI)
static void sci_mode_change_timeout_fn(struct k_work *work);
#endif

static struct peripheral_slot *slot_by_conn(struct bt_conn *conn)
{
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].conn == conn) {
			return &slots[i];
		}
	}

	return NULL;
}

static unsigned int active_connection_count(void)
{
	unsigned int n = 0;

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].conn) {
			n++;
		}
	}

	return n;
}

static struct peripheral_slot *slot_alloc_for_connection(struct bt_conn *conn)
{
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (!slots[i].conn) {
			slots[i].conn = bt_conn_ref(conn);
			return &slots[i];
		}
	}

	return NULL;
}

static void scanning_continue(void)
{
	int err;

	if (active_connection_count() < PERIPHERAL_SLOT_COUNT) {
		err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
		if (!err) {
			printk("Scanning successfully started\n");
		} else if (err != -EALREADY) {
			printk("Scanning failed to start (err %d)\n", err);
		}
	}
}

#if PERIPHERAL_SLOT_COUNT > 1
static bool peer_acl_exists(const bt_addr_le_t *peer)
{
	struct bt_conn *existing;

	existing = bt_conn_lookup_addr_le(BT_ID_DEFAULT, peer);
	if (!existing) {
		return false;
	}

	bt_conn_unref(existing);
	return true;
}
#endif

static void try_connect(struct bt_scan_device_info *device_info,
			bool connectable)
{
	int err;
	struct bt_conn *conn = NULL;
	const bt_addr_le_t *peer = device_info->recv_info->addr;

	if (!connectable || active_connection_count() >= PERIPHERAL_SLOT_COUNT) {
		return;
	}

	bt_scan_stop();

	err = bt_conn_le_create(peer, BT_CONN_LE_CREATE_CONN,
				device_info->conn_param, &conn);
	if (err) {
		printk("Connecting failed\n");
		scanning_continue();
		return;
	}

	struct peripheral_slot *slot = slot_alloc_for_connection(conn);

	if (!slot) {
		printk("No free peripheral slot; disconnecting\n");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
	bt_conn_unref(conn);
}

K_THREAD_STACK_DEFINE(cont_report_rx_stats_print_stack,
		      CONT_REPORT_RX_STATS_PRINT_STACK_SIZE);
static struct k_thread cont_report_rx_stats_print_thread_data;

static void cont_report_rx_data_reset(struct peripheral_slot *slot)
{
	slot->cont_report_rx_max_interval_us = 0;
	slot->cont_report_rx_min_interval_us = UINT32_MAX;
	slot->cont_report_rx_above_window_count = 0;
	slot->cont_report_rx_below_window_count = 0;

	slot->cont_report_rx_previous_report_cycles = 0;
	slot->hogp_notify_batch_start_cycles = 0;
	slot->hogp_notify_cnt = 0;
}

K_MSGQ_DEFINE(cont_report_rx_dataq, sizeof(struct cont_report_rx_data),
	      CONT_REPORT_RX_STATS_MSGQ_MAX_MESSAGES, sizeof(uint32_t));

static void cont_report_rx_stats_print_thread_fn(void *p1, void *p2, void *p3)
{
	struct cont_report_rx_data msg;
	uint32_t avg_interval_us;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_msgq_get(&cont_report_rx_dataq, &msg, K_FOREVER);

		printk("\n");
		PRINT_PERIPH_INDEX_STR(msg.peripheral_idx);
		printk("Received %d reports, in %d us\n", msg.notify_cnt, msg.total_time_us);
		if (msg.notify_cnt > 1) {
			avg_interval_us = msg.total_time_us / (msg.notify_cnt - 1);
			printk("Average report interval: %u us, max %u us, min %u us\n",
				avg_interval_us, msg.max_interval_us, msg.min_interval_us);
		}
		if (msg.report_interval_us > 0) {
			printk("Expected report interval: %u us\n", msg.report_interval_us);
			printk("Intervals above %d us: %d\n",
				msg.report_interval_us
				+ CONTINUOUS_REPORT_RECEIVING_WINDOW_DELTA_US,
				msg.above_window_count);
			printk("Intervals below %d us: %d\n",
				msg.report_interval_us
				- CONTINUOUS_REPORT_RECEIVING_WINDOW_DELTA_US,
				msg.below_window_count);
		}
	}
}

static void cont_rx_stats_print_thread_start(void)
{
	k_thread_create(&cont_report_rx_stats_print_thread_data, cont_report_rx_stats_print_stack,
			K_THREAD_STACK_SIZEOF(cont_report_rx_stats_print_stack),
			cont_report_rx_stats_print_thread_fn, NULL,
			NULL, NULL, CONT_REPORT_RX_STATS_PRINT_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&cont_report_rx_stats_print_thread_data, "cont_report_rx_stats_print");
}

static void cont_rx_stats_print_thread_push(struct peripheral_slot *slot,
					    uint32_t batch_end_cycles)
{
	struct cont_report_rx_data cont_rx_stats_msg = {
		.peripheral_idx = PERIPH_SLOT_INDEX(slot),
		.notify_cnt = slot->hogp_notify_cnt,
		.report_interval_us = slot->cont_report_rx_interval_us,
		.total_time_us = k_cyc_to_us_floor32(batch_end_cycles
						     - slot->hogp_notify_batch_start_cycles),
		.max_interval_us = slot->cont_report_rx_max_interval_us,
		.min_interval_us = slot->cont_report_rx_min_interval_us,
		.above_window_count = slot->cont_report_rx_above_window_count,
		.below_window_count = slot->cont_report_rx_below_window_count,
	};

	if (k_msgq_put(&cont_report_rx_dataq, &cont_rx_stats_msg, K_NO_WAIT) != 0) {
		printk("cont report rx stats queue full\n");
	}
}

static void cont_report_rx_timer_expire(struct k_timer *timer)
{
	struct peripheral_slot *slot =
		CONTAINER_OF(timer, struct peripheral_slot, cont_report_rx_timer);

	if (slot->hogp_notify_cnt > 0) {
		cont_rx_stats_print_thread_push(slot, slot->cont_report_rx_previous_report_cycles);
	}

	atomic_set(&slot->cont_report_rx_on, 0);
	printk("\n");
	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
	printk("Continuous report receiving disabled due to prolonged inactivity\n");
}

static void cont_report_rx_timer_restart(struct peripheral_slot *slot)
{
	k_timer_start(&slot->cont_report_rx_timer,
		      K_USEC(CONTINUOUS_REPORT_RECEIVING_EXIT_THRESHOLD_US),
		      K_NO_WAIT);
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!filter_match->uuid.match ||
	    (filter_match->uuid.count != 1)) {

		printk("Invalid device connected\n");

		return;
	}

	const struct bt_uuid *uuid = filter_match->uuid.uuid[0];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Filters matched on UUID 0x%04x.\nAddress: %s connectable: %s\n",
		BT_UUID_16(uuid)->val,
		addr, connectable ? "yes" : "no");

#if PERIPHERAL_SLOT_COUNT > 1
	if (peer_acl_exists(device_info->recv_info->addr)) {
		return;
	}
#endif

	try_connect(device_info, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	printk("Connecting failed\n");
	scanning_continue();
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	struct peripheral_slot *slot = slot_alloc_for_connection(conn);
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	printk("Scanning connecting on address: %s\n", addr);

	if (!slot) {
		printk("No free peripheral slot; disconnecting\n");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}
/** .. include_startingpoint_scan_rst */
static void scan_filter_no_match(struct bt_scan_device_info *device_info,
				 bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	ARG_UNUSED(connectable);

#if PERIPHERAL_SLOT_COUNT > 1
	if (peer_acl_exists(device_info->recv_info->addr)) {
		return;
	}
#endif

	if (device_info->recv_info->adv_type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		bt_addr_le_to_str(device_info->recv_info->addr, addr,
				  sizeof(addr));
		printk("Direct advertising received from %s\n", addr);

		try_connect(device_info, true);
	}
}
/** .. include_endpoint_scan_rst */
BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
		scan_connecting_error, scan_connecting);

static void discovery_completed_cb(struct bt_gatt_dm *dm,
				   void *context)
{
	struct peripheral_slot *slot = context;
	int err;

	if (slot == NULL) {
		printk("Discovery completed, but peripheral slot is NULL\n");
		scanning_continue();
		return;
	}

	printk("The discovery procedure succeeded\n");

	bt_gatt_dm_data_print(dm);

	err = bt_hogp_handles_assign(dm, &slot->hogp);
	if (err) {
		printk("Could not init HIDS client object, error: %d\n", err);
		scanning_continue();
	}

	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Could not release the discovery data, error "
		       "code: %d\n", err);
		scanning_continue();
	}
}

static void discovery_service_not_found_cb(struct bt_conn *conn,
					   void *context)
{
	printk("The service could not be found during the discovery\n");
	scanning_continue();
}

static void discovery_error_found_cb(struct bt_conn *conn,
				     int err,
				     void *context)
{
	printk("The discovery procedure failed with %d\n", err);
	scanning_continue();
}

static const struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

static void gatt_discover(struct bt_conn *conn)
{
	struct peripheral_slot *slot = slot_by_conn(conn);
	int err;

	if (!slot) {
		printk("Peripheral slot is NULL\n");
		scanning_continue();
		return;
	}

	err = bt_gatt_dm_start(conn, BT_UUID_HIDS, &discovery_cb, slot);
	if (err) {
		printk("could not start the discovery procedure, error "
			"code: %d\n", err);
		scanning_continue();
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN];
	struct peripheral_slot *slot = slot_by_conn(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s, 0x%02x %s\n", addr, conn_err,
		       bt_hci_err_to_str(conn_err));
		if (slot) {
			bt_conn_unref(slot->conn);
			slot->conn = NULL;
			scanning_continue();
		}

		return;
	}

	printk("Connected: %s\n", addr);

	if (slot) {
		struct bt_conn_info conn_info;

		err = bt_conn_get_info(conn, &conn_info);
		if (!err) {
			printk("Connection interval: %u us\n", conn_info.le.interval_us);
			/** Currently connection interval is equal to expected report interval
			 *  This will however not be the case if subrating is enabled.
			 */
			slot->cont_report_rx_interval_us = conn_info.le.interval_us;
			printk("Expected report interval: %u us\n", slot->cont_report_rx_interval_us);
		} else {
			printk("Failed to get connection interval\n");
			slot->cont_report_rx_interval_us = 0;
		}
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		printk("Failed to set security: %d\n", err);

		gatt_discover(conn);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct peripheral_slot *slot = slot_by_conn(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	printk("Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	if (!slot) {
		return;
	}

	atomic_set(&slot->cont_report_rx_on, 0);
	k_timer_stop(&slot->cont_report_rx_timer);
	cont_report_rx_data_reset(slot);

	if (bt_hogp_assign_check(&slot->hogp)) {
		printk("HIDS client active - releasing");
		bt_hogp_release(&slot->hogp);
	}

	bt_conn_unref(slot->conn);
	slot->conn = NULL;

	scanning_continue();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
		gatt_discover(conn);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
		scanning_continue();
	}
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			      uint16_t latency, uint16_t timeout)
{
	struct peripheral_slot *slot = slot_by_conn(conn);

	ARG_UNUSED(latency);
	ARG_UNUSED(timeout);

	if (!slot) {
		return;
	}

	if (slot->hogp_notify_cnt > 0) {
		cont_rx_stats_print_thread_push(slot, slot->cont_report_rx_previous_report_cycles);
	}
	cont_report_rx_data_reset(slot);
	slot->cont_report_rx_interval_us = BT_CONN_INTERVAL_TO_US(interval);
	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
	printk("Connection interval updated to %u us\n",
	       slot->cont_report_rx_interval_us);
}

#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS)
static void conn_rate_changed(struct bt_conn *conn, uint8_t status,
			      const struct bt_conn_le_conn_rate_changed *params)
{
	struct peripheral_slot *slot = slot_by_conn(conn);

	if (!slot) {
		return;
	}

	if (status != BT_HCI_ERR_SUCCESS || params == NULL) {
		return;
	}

	if (slot->hogp_notify_cnt > 0) {
		cont_rx_stats_print_thread_push(slot, slot->cont_report_rx_previous_report_cycles);
	}
	cont_report_rx_data_reset(slot);
	slot->cont_report_rx_interval_us = params->interval_us;
	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
	printk("Connection interval updated to %u us\n",
	       slot->cont_report_rx_interval_us);
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.security_changed = security_changed,
	.le_param_updated = le_param_updated,
#if defined(CONFIG_BT_SHORTER_CONNECTION_INTERVALS)
	.conn_rate_changed = conn_rate_changed,
#endif
};

static void scan_init(void)
{
	int err;

	struct bt_scan_init_param scan_init = {
		/* Manual connect on filter match avoids duplicate connection attempts when a
		 * peripheral keeps advertising after the central connects.
		 */
		.connect_if_match = false,
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HIDS);
	if (err) {
		printk("Scanning filters cannot be set (err %d)\n", err);

		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("Filters cannot be turned on (err %d)\n", err);
	}
}

static uint8_t hogp_notify_cb(struct bt_hogp *hogp,
			     struct bt_hogp_rep_info *rep,
			     uint8_t err,
			     const uint8_t *data)
{
	struct peripheral_slot *slot = CONTAINER_OF(hogp, struct peripheral_slot, hogp);
	bool first_in_batch = false;
	uint32_t now = k_cycle_get_32();
	uint32_t last = slot->cont_report_rx_previous_report_cycles;

	slot->cont_report_rx_previous_report_cycles = now;

	if (!data) {
		return BT_GATT_ITER_STOP;
	}

	if (slot->hogp_notify_cnt == 0U) {
		/* First report in the batch */
		slot->hogp_notify_batch_start_cycles = now;
		first_in_batch = true;
	}

	slot->hogp_notify_cnt++;

	if (atomic_get(&slot->cont_report_rx_on)) {
		if (first_in_batch) {
			/* Nothing to calculate for the first report in the batch */
			return BT_GATT_ITER_CONTINUE;
		}

		uint32_t time_elapsed = k_cyc_to_us_floor32(now - last);

		if (time_elapsed > slot->cont_report_rx_max_interval_us) {
			slot->cont_report_rx_max_interval_us = time_elapsed;
		}
		if (time_elapsed < slot->cont_report_rx_min_interval_us) {
			slot->cont_report_rx_min_interval_us = time_elapsed;
		}
		if (slot->cont_report_rx_interval_us > 0) {
			if (time_elapsed > slot->cont_report_rx_interval_us
					    + CONTINUOUS_REPORT_RECEIVING_WINDOW_DELTA_US) {
				slot->cont_report_rx_above_window_count++;
			}
			if (time_elapsed < slot->cont_report_rx_interval_us
						   - CONTINUOUS_REPORT_RECEIVING_WINDOW_DELTA_US) {
				slot->cont_report_rx_below_window_count++;
			}
		}

		if (slot->hogp_notify_cnt %
		    CONTINUOUS_REPORT_RECEIVING_PRINT_INFO_RATE == 0) {
			/*
			 * Offload printk so it does not perturb notification timing.
			 */
			cont_rx_stats_print_thread_push(slot, now);
			cont_report_rx_data_reset(slot);
		}

		cont_report_rx_timer_restart(slot);
		return BT_GATT_ITER_CONTINUE;
	}

	if (slot->hogp_notify_cnt % CONT_REPORT_RX_ENTRANCE_SAMPLE_REPORTS == 0) {
		/*
		 * Note: theoretically we could hit this condition if a notification is
		 * received right after the counter wrapped around, but this is extremely
		 * unlikely and the consequences are negligible. No extra check keeps the
		 * code simpler.
		 */
		if (k_cyc_to_us_floor32(now - slot->hogp_notify_batch_start_cycles)
		    < CONTINUOUS_REPORT_RECEIVING_ENTRANCE_THRESHOLD_US(slot)) {
			atomic_set(&slot->cont_report_rx_on, 1);
			PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
			printk("Continuous report receiving enabled due to"
			       " short time interval between notifications.\n"
			       "Expected report interval: %u us\n",
			       slot->cont_report_rx_interval_us);
			cont_report_rx_data_reset(slot);
			cont_report_rx_timer_restart(slot);
			return BT_GATT_ITER_CONTINUE;
		}

		slot->hogp_notify_cnt = 0;
	}

	uint8_t i;
	uint8_t size = bt_hogp_rep_size(rep);

	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));

	printk("Notification, id: %u, size: %u, data:",
	       bt_hogp_rep_id(rep),
	       size);
	for (i = 0; i < size; ++i) {
		printk(" 0x%x", data[i]);
	}
	printk("\n");
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t hogp_boot_mouse_report(struct bt_hogp *hogp,
				     struct bt_hogp_rep_info *rep,
				     uint8_t err,
				     const uint8_t *data)
{
	ARG_UNUSED(err);

	uint8_t size = bt_hogp_rep_size(rep);
	uint8_t i;

	if (!data) {
		return BT_GATT_ITER_STOP;
	}

#if PERIPHERAL_SLOT_COUNT > 1
	struct peripheral_slot *slot = CONTAINER_OF(hogp, struct peripheral_slot, hogp);
	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
#endif
	printk("Notification, mouse boot, size: %u, data:", size);
	for (i = 0; i < size; ++i) {
		printk(" 0x%x", data[i]);
	}
	printk("\n");
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t hogp_boot_kbd_report(struct bt_hogp *hogp,
				   struct bt_hogp_rep_info *rep,
				   uint8_t err,
				   const uint8_t *data)
{
	ARG_UNUSED(err);

	uint8_t size = bt_hogp_rep_size(rep);
	uint8_t i;

	if (!data) {
		return BT_GATT_ITER_STOP;
	}

#if PERIPHERAL_SLOT_COUNT > 1
	struct peripheral_slot *slot = CONTAINER_OF(hogp, struct peripheral_slot, hogp);
	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
#endif
	printk("Notification, keyboard boot, size: %u, data:", size);
	for (i = 0; i < size; ++i) {
		printk(" 0x%x", data[i]);
	}
	printk("\n");
	return BT_GATT_ITER_CONTINUE;
}

static void hogp_ready_cb(struct bt_hogp *hogp)
{
	struct peripheral_slot *slot = CONTAINER_OF(hogp, struct peripheral_slot, hogp);

	k_work_submit(&slot->hids_ready_work);
}

#if defined(CONFIG_BT_HOGP_SCI)
static const char *sci_mode_to_string(enum bt_hids_sci_mode_value mode) {
	switch (mode) {
	case BT_HIDS_SCI_MODE_NONE:
		return "NONE";
	case BT_HIDS_SCI_MODE_DEFAULT:
		return "DEFAULT";
	case BT_HIDS_SCI_MODE_FAST:
		return "FAST";
	case BT_HIDS_SCI_MODE_LOW_POWER:
		return "LOW_POWER";
	case BT_HIDS_SCI_MODE_FULL_RANGE:
		return "FULL_RANGE";
	}

	return "UNKNOWN";
}

static void sci_mode_change_timeout_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct peripheral_slot *slot = CONTAINER_OF(dwork, struct peripheral_slot, sci_mode_timeout);

	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
	printk("SCI mode change timeout occurred.\n");
	slot->sci_mode_requested = BT_HIDS_SCI_MODE_NONE;
}

static void sci_mode_notify_cb(struct bt_conn *conn, const uint8_t mode)
{
	struct peripheral_slot *slot = slot_by_conn(conn);
	const char *mode_str = sci_mode_to_string(mode);

	if (!slot) {
		return;
	}

	PRINT_PERIPH_INDEX_STR(PERIPH_SLOT_INDEX(slot));
	printk("SCI mode changed notification received, new mode: %s\n", mode_str);

	if (mode == slot->sci_mode_requested) {
		k_work_cancel_delayable(&slot->sci_mode_timeout);
		slot->sci_mode_requested = BT_HIDS_SCI_MODE_NONE;
		return;
	}

	printk("The new SCI mode does not match the requested one\n");
}
#endif

static void hids_on_ready(struct k_work *work)
{
	struct peripheral_slot *slot = CONTAINER_OF(work, struct peripheral_slot, hids_ready_work);
	struct bt_hogp *hogp = &slot->hogp;
	int err;
	struct bt_hogp_rep_info *rep = NULL;

	printk("HIDS is ready to work\n");

	while (NULL != (rep = bt_hogp_rep_next(hogp, rep))) {
		if (bt_hogp_rep_type(rep) ==
		    BT_HIDS_REPORT_TYPE_INPUT) {
			printk("Subscribe to report id: %u\n",
			       bt_hogp_rep_id(rep));
			err = bt_hogp_rep_subscribe(hogp, rep,
							   hogp_notify_cb);
			if (err) {
				printk("Subscribe error (%d)\n", err);
			}
		}
	}
	if (hogp->rep_boot.kbd_inp) {
		printk("Subscribe to boot keyboard report\n");
		err = bt_hogp_rep_subscribe(hogp,
					   hogp->rep_boot.kbd_inp,
					   hogp_boot_kbd_report);
		if (err) {
			printk("Subscribe error (%d)\n", err);
		}
	}
	if (hogp->rep_boot.mouse_inp) {
		printk("Subscribe to boot mouse report\n");
		err = bt_hogp_rep_subscribe(hogp,
					   hogp->rep_boot.mouse_inp,
					   hogp_boot_mouse_report);
		if (err) {
			printk("Subscribe error (%d)\n", err);
		}
	}

#if defined(CONFIG_BT_HOGP_SCI)
	err = bt_hogp_sci_mode_subscribe(hogp, sci_mode_notify_cb);
	if (err) {
		printk("SCI mode subscribe error (%d)\n", err);
	}
#endif
	scanning_continue();
}

static void hogp_prep_fail_cb(struct bt_hogp *hogp, int err)
{
	ARG_UNUSED(hogp);
	ARG_UNUSED(err);

	printk("ERROR: HIDS client preparation failed!\n");
	scanning_continue();
}

static void hogp_pm_update_cb(struct bt_hogp *hogp)
{
	printk("Protocol mode updated: %s\n",
	      bt_hogp_pm_get(hogp) == BT_HIDS_PM_BOOT ?
	      "BOOT" : "REPORT");
}

/* HIDS client initialization parameters */
static const struct bt_hogp_init_params hogp_init_params = {
	.ready_cb      = hogp_ready_cb,
	.prep_error_cb = hogp_prep_fail_cb,
	.pm_update_cb  = hogp_pm_update_cb
};


static void button_bootmode(void)
{
	int err;
	bool any_device_ready = false;

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		struct bt_hogp *hogp = &slots[i].hogp;
		enum bt_hids_pm pm;
		enum bt_hids_pm new_pm;

		if (!bt_hogp_ready_check(hogp)) {
			continue;
		}

		any_device_ready = true;
		pm = bt_hogp_pm_get(hogp);
		new_pm = ((pm == BT_HIDS_PM_BOOT) ? BT_HIDS_PM_REPORT : BT_HIDS_PM_BOOT);

		PRINT_PERIPH_INDEX_STR(i);
		printk("Setting protocol mode: %s\n",
		       (new_pm == BT_HIDS_PM_BOOT) ? "BOOT" : "REPORT");
		err = bt_hogp_pm_write(hogp, new_pm);
		if (err) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("Cannot change protocol mode, err: %d\n", err);
		}
	}

	if (!any_device_ready) {
		printk("No HID device ready\n");
	}
}

static void hidc_write_cb(struct bt_hogp *hidc,
			  struct bt_hogp_rep_info *rep,
			  uint8_t err)
{
	printk("Caps lock sent\n");
}

static void button_capslock(void)
{
	int err;
	uint8_t data;
	bool any_device_ready = false;
	bool any_keyboard_out = false;

	capslock_state = capslock_state ? 0 : 1;
	data = capslock_state ? 0x02 : 0;

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		struct bt_hogp *hogp = &slots[i].hogp;

		if (!bt_hogp_ready_check(hogp)) {
			continue;
		}

		any_device_ready = true;

		if (!hogp->rep_boot.kbd_out) {
			continue;
		}

		any_keyboard_out = true;

		if (bt_hogp_pm_get(hogp) != BT_HIDS_PM_BOOT) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("This function works only in BOOT Report mode\n");
			continue;
		}

		err = bt_hogp_rep_write_wo_rsp(hogp, hogp->rep_boot.kbd_out,
					       &data, sizeof(data),
					       hidc_write_cb);

		if (!err) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("Caps lock sent, val: 0x%x\n", data);
		} else {
			PRINT_PERIPH_INDEX_STR(i);
			printk("Keyboard data write error, err: %d\n", err);
		}
	}

	if (!any_device_ready) {
		printk("No HID device ready\n");
		return;
	}

	if (!any_keyboard_out) {
		printk("No HID device with keyboard OUT report\n");
	}
}


static uint8_t capslock_read_cb(struct bt_hogp *hogp,
			     struct bt_hogp_rep_info *rep,
			     uint8_t err,
			     const uint8_t *data)
{
	if (err) {
		printk("Capslock read error (err: %u)\n", err);
		return BT_GATT_ITER_STOP;
	}
	if (!data) {
		printk("Capslock read - no data\n");
		return BT_GATT_ITER_STOP;
	}
	printk("Received data (size: %u, data[0]: 0x%x)\n",
	       bt_hogp_rep_size(rep), data[0]);

	return BT_GATT_ITER_STOP;
}


static void capslock_write_cb(struct bt_hogp *hogp,
			      struct bt_hogp_rep_info *rep,
			      uint8_t err)
{
	int ret;

	printk("Capslock write result: %u\n", err);

	ret = bt_hogp_rep_read(hogp, rep, capslock_read_cb);
	if (ret) {
		printk("Cannot read capslock value (err: %d)\n", ret);
	}
}


static void button_capslock_rsp(void)
{
	int err;
	uint8_t data;

	capslock_state = capslock_state ? 0 : 1;
	data = capslock_state ? 0x02 : 0;

	/* One outstanding write-with-response at a time across GATT. */
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		struct bt_hogp *hogp = &slots[i].hogp;

		if (!bt_hogp_ready_check(hogp)) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("HID device not ready\n");
			continue;
		}

		if (!hogp->rep_boot.kbd_out) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("HID device missing keyboard OUT report\n");
			continue;
		}

		err = bt_hogp_rep_write(hogp, hogp->rep_boot.kbd_out, capslock_write_cb,
					&data, sizeof(data));
		if (err) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("Keyboard data write error, err: %d\n", err);
			return;
		}

		PRINT_PERIPH_INDEX_STR(i);
		printk("Caps lock send using write with response, val: 0x%x\n", data);
	}
}


static void num_comp_reply(bool accept)
{
	if (accept) {
		bt_conn_auth_passkey_confirm(auth_conn);
		printk("Numeric Match, conn %p\n", auth_conn);
	} else {
		bt_conn_auth_cancel(auth_conn);
		printk("Numeric Reject, conn %p\n", auth_conn);
	}

	bt_conn_unref(auth_conn);
	auth_conn = NULL;
}

#if defined(CONFIG_BT_HOGP_SCI)
static int set_default_conn_rate(void)
{
	int err;
	uint16_t local_min_interval_us;
	uint16_t interval_min_125us = CONFIG_CENTRAL_HIDS_SCI_INTERVAL_MIN_125US;

	err = bt_conn_le_read_min_conn_interval(&local_min_interval_us);
	if (err) {
		printk("Failed to read min conn interval (err %d)\n", err);
	}

	if (!err && interval_min_125us < local_min_interval_us / 125U) {
		printk("Configured minimum connection interval (%u) is below controller "
		       "minimum %u; using %u\n",
		       interval_min_125us, local_min_interval_us/ 125U,
		       local_min_interval_us / 125U);

		interval_min_125us = local_min_interval_us / 125U;
		if (interval_min_125us > CONFIG_CENTRAL_HIDS_SCI_INTERVAL_MAX_125US) {
			printk("ERROR: controller connection interval minimum is larger "
			       "than configured maximum (%u > %u)!\n",
			       interval_min_125us, CONFIG_CENTRAL_HIDS_SCI_INTERVAL_MAX_125US);
			return -EINVAL;
		}
	}
	printk("Min conn interval: %u us\n", local_min_interval_us);

	const struct bt_conn_le_conn_rate_param params = {
		.interval_min_125us = interval_min_125us,
		.interval_max_125us = CONFIG_CENTRAL_HIDS_SCI_INTERVAL_MAX_125US,
		.subrate_min = CONFIG_CENTRAL_HIDS_SCI_SUBRATE_MIN,
		.subrate_max = CONFIG_CENTRAL_HIDS_SCI_SUBRATE_MAX,
		.max_latency = CONFIG_CENTRAL_HIDS_SCI_MAX_LATENCY,
		.continuation_number = CONFIG_CENTRAL_HIDS_SCI_CONTINUATION_NUM,
		.supervision_timeout_10ms = CONFIG_CENTRAL_HIDS_SCI_SUPERVISION_TIMEOUT_10MS,
		.min_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MIN_125US,
		.max_ce_len_125us = BT_HCI_LE_SCI_CE_LEN_MAX_125US,
	};

	return bt_conn_le_conn_rate_set_defaults(&params);
}

static void button_sci_mode_next(void)
{
	static const uint8_t mode_val[] = {
		BT_HIDS_SCI_MODE_DEFAULT,
		BT_HIDS_SCI_MODE_FAST,
		BT_HIDS_SCI_MODE_LOW_POWER,
		BT_HIDS_SCI_MODE_FULL_RANGE,
	};
	static uint8_t sci_mode_idx = 0;
	const uint8_t mode = mode_val[sci_mode_idx];
	bool any_ok = false;

	printk("Requesting SCI mode %s on all ready devices\n", sci_mode_to_string(mode));

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		struct bt_hogp *hogp = &slots[i].hogp;

		if (!bt_hogp_ready_check(hogp)) {
			continue;
		}
		if (!bt_hogp_sci_supported(hogp)) {
			continue;
		}

		const struct bt_hids_info *info = bt_hogp_conn_info_val(hogp);

		if (!(info->flags & BT_HIDS_SCI_SUPPORTED)) {
			continue;
		}
		if (slots[i].sci_mode_requested != BT_HIDS_SCI_MODE_NONE) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("SCI mode request already in progress, skipping\n");
			continue;
		}

		if (mode == BT_HIDS_SCI_MODE_LOW_POWER &&
		    !bt_hogp_sci_low_power_mode_supported(hogp)) {
			PRINT_PERIPH_INDEX_STR(i);
			printk("Device doesn't support SCI LOW POWER mode, skipping\n");
			continue;
		}

		int err = bt_hogp_sci_mode_req(hogp, mode);

		if (!err) {
			any_ok = true;
			PRINT_PERIPH_INDEX_STR(i);
			printk("Sent %s SCI mode request to the device\n",
			       sci_mode_to_string(mode));
			slots[i].sci_mode_requested = mode;
			k_work_schedule(&slots[i].sci_mode_timeout,
					K_MSEC(SCI_MODE_CHANGE_TIMEOUT_MS));
		} else {
			PRINT_PERIPH_INDEX_STR(i);
			printk("SCI mode request failed (err: %d)\n", err);
		}
	}

	if (!any_ok) {
		printk("No HID device accepted SCI mode change\n");
		return;
	}

	sci_mode_idx = (sci_mode_idx + 1) % ARRAY_SIZE(mode_val);
}
#endif

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button = button_state & has_changed;

	if (btn_additional_functions_active) {
#if defined(CONFIG_BT_HOGP_SCI)
		if (button & KEY_ADDITIONAL_FUNCTIONS_SCI_MODE_NEXT) {
			button_sci_mode_next();
		}
#endif
		if (button & KEY_ADDITIONAL_FUNCTIONS_MASK) {
			btn_additional_functions_active = false;
			printk("Additional button functions deactivated\n");
		}
		return;
	}

	if (auth_conn) {
		if (button & KEY_PAIRING_ACCEPT) {
			num_comp_reply(true);
		}

		if (button & KEY_PAIRING_REJECT) {
			num_comp_reply(false);
		}

		return;
	}

	if (button & KEY_BOOTMODE_MASK) {
		button_bootmode();
	}
	if (button & KEY_CAPSLOCK_MASK) {
		button_capslock();
	}
	if (button & KEY_CAPSLOCK_RSP_MASK) {
		button_capslock_rsp();
	}
	if (button & KEY_ADDITIONAL_FUNCTIONS_MASK) {
		btn_additional_functions_active = true;
		printk("Additional button functions activated.\n");
		if (IS_ENABLED(CONFIG_SOC_SERIES_NRF54H) || IS_ENABLED(CONFIG_SOC_SERIES_NRF54L)) {
#if defined(CONFIG_BT_HOGP_SCI)
			printk("Button 1: Next SCI mode\n");
#else
			printk("No additional button functions available\n");
#endif
			printk("Button 3: Exit additional button functions\n");
		} else {
#if defined(CONFIG_BT_HOGP_SCI)
			printk("Button 2: Next SCI mode\n");
#else
			printk("No additional button functions available\n");
#endif
			printk("Button 4: Exit additional button functions\n");
		}
	}
}


static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}


static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);

	if (IS_ENABLED(CONFIG_SOC_SERIES_NRF54H) || IS_ENABLED(CONFIG_SOC_SERIES_NRF54L)) {
		printk("Press Button 0 to confirm, Button 1 to reject.\n");
	} else {
		printk("Press Button 1 to confirm, Button 2 to reject.\n");
	}
}


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));

	scanning_continue();
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};


int main(void)
{
	int err;

	printk("Starting Bluetooth Central HIDS sample\n");

	cont_rx_stats_print_thread_start();

	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		atomic_set(&slots[i].cont_report_rx_on, 0);
		slots[i].cont_report_rx_min_interval_us = UINT32_MAX;
		k_timer_init(&slots[i].cont_report_rx_timer, cont_report_rx_timer_expire, NULL);
		bt_hogp_init(&slots[i].hogp, &hogp_init_params);
		k_work_init(&slots[i].hids_ready_work, hids_on_ready);
#if defined(CONFIG_BT_HOGP_SCI)
		k_work_init_delayable(&slots[i].sci_mode_timeout, sci_mode_change_timeout_fn);
		slots[i].sci_mode_requested = BT_HIDS_SCI_MODE_NONE;
#endif
	}

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		printk("failed to register authorization callbacks.\n");
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		printk("Failed to register authorization info callbacks.\n");
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

#if defined(CONFIG_BT_HOGP_SCI)
	err = set_default_conn_rate();
	if (err) {
		printk("Failed to set the default connection rate (err %d)\n", err);
		return 0;
	}
#endif

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	scan_init();

	err = dk_buttons_init(button_handler);
	if (err) {
		printk("Failed to initialize buttons (err %d)\n", err);
		return 0;
	}

	scanning_continue();
	return 0;
}
