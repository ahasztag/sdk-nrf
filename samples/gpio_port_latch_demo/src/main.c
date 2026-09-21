/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#if !DT_NODE_EXISTS(DT_ALIAS(sw0)) || !DT_NODE_EXISTS(DT_ALIAS(sw1)) || \
	!DT_NODE_EXISTS(DT_ALIAS(sw2))
#error "Board must define sw0, sw1 and sw2 aliases (buttons 0-2 on DK)"
#endif

#define NUM_BUTTONS 3
#define ISR_LOG_SIZE 8

static const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
};

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(DT_ALIAS(sw0), gpios),
			  DT_GPIO_CTLR(DT_ALIAS(sw1), gpios)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(DT_ALIAS(sw0), gpios),
			  DT_GPIO_CTLR(DT_ALIAS(sw2), gpios)));

/* Debugger-visible state updated from the GPIO callback. */
atomic_t isr_count;
volatile uint32_t isr_pins_log[ISR_LOG_SIZE];
volatile uint8_t isr_log_idx;

static struct gpio_callback gpio_cb;

static void gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);

	if (isr_log_idx >= ISR_LOG_SIZE) {
		return;
	}

	atomic_inc(&isr_count);
	isr_pins_log[isr_log_idx] = pins;
	isr_log_idx++;

	for (size_t i = 0; i < NUM_BUTTONS; i++) {
		if (!(pins & BIT(buttons[i].pin))) {
			continue;
		}

		gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_DISABLE);
	}

}

int main(void)
{
	uint32_t pin_mask = 0;
	int err;

	for (size_t i = 0; i < NUM_BUTTONS; i++) {
		if (!gpio_is_ready_dt(&buttons[i])) {
			return -ENODEV;
		}

		err = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		if (err != 0) {
			return err;
		}

		pin_mask |= BIT(buttons[i].pin);
	}

	gpio_init_callback(&gpio_cb, gpio_isr, pin_mask);
	err = gpio_add_callback(buttons[0].port, &gpio_cb);
	if (err != 0) {
		return err;
	}

	for (size_t i = 0; i < NUM_BUTTONS; i++) {
		err = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_LEVEL_LOW);
		if (err != 0) {
			return err;
		}
	}

	while (true) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
