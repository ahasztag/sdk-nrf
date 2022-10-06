/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>

#include "mpsl_fem_power_model_interface.h"

#include <mpsl_fem_power_model.h>
#include <mpsl_temp.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>


static int32_t last_temperature = INT32_MIN;

static void model_update_perform(void)
{
	mpsl_fem_external_conditions_t external_conditions;
	int32_t current_temperature = mpsl_temperature_get();

	if (last_temperature != current_temperature) {
		external_conditions.temperature = current_temperature / 4;
		external_conditions.voltage = CONFIG_MPSL_FEM_POWER_VOLTAGE / 100;
		mpsl_fem_nrf21540_power_model_builtin_update(&external_conditions);
        last_temperature = current_temperature;
	}
}

static void model_update_thread_function(void *unused1, void *unused2, void *unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

	while (true)
	{
        k_sleep(K_MSEC(CONFIG_MPSL_FEM_BUILTIN_POWER_MODEL_UPDATE_PERIOD));
        model_update_perform();
	}
}

static K_THREAD_STACK_DEFINE(model_thread_stack, 512);
struct k_thread model_update_thread;

static int model_periodic_update_start(const struct device *unused)
{
	/* Perform initial model update */
	model_update_perform();

	k_thread_create(&model_update_thread, model_thread_stack,
	                K_THREAD_STACK_SIZEOF(model_thread_stack),
	                model_update_thread_function,
	                NULL, NULL, NULL,
	                K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

	return 0;
}

const mpsl_fem_power_model_t *mpsl_fem_power_model_to_use_get(void)
{
	return mpsl_fem_nrf21540_power_model_builtin_get();
}

SYS_INIT(model_periodic_update_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

