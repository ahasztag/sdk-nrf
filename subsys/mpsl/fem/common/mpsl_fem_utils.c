/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <mpsl_fem_utils.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/__assert.h>

#if defined(CONFIG_MPSL_FEM_PIN_FORWARDER)


void mpsl_fem_pin_extend_with_port(uint8_t *pin, const char *lbl)
{
	/* The pin numbering in the FEM configuration API follows the
	 * convention:
	 *   pin numbers 0..31 correspond to the gpio0 port
	 *   pin numbers 32..63 correspond to the gpio1 port
	 *
	 * Values obtained from devicetree are here adjusted to the ranges
	 * given above.
	 */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio0), okay)
	if (strcmp(lbl, DT_LABEL(DT_NODELABEL(gpio0))) == 0) {
		return;
	}
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
	if (strcmp(lbl, DT_LABEL(DT_NODELABEL(gpio1))) == 0) {
		*pin += 32;
		return;
	}
#endif
	(void)pin;

	__ASSERT(false, "Unknown GPIO port DT label");
}

#else

static void pin_num_to_gpio_lbl_and_pin(uint8_t raw_pin, const char **gpio_lbl, uint8_t* port_pin)
{
    *port_pin = raw_pin;

    if (raw_pin < 32) {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio0), okay)
        *gpio_lbl = DT_LABEL(DT_NODELABEL(gpio0));
#else
        __ASSERT(false, "Unknown GPIO port DT label");
#endif
    } else {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
        *port_pin -= 32;
        *gpio_lbl = DT_LABEL(DT_NODELABEL(gpio1));
#else
        __ASSERT(false, "Unknown GPIO port DT label");
#endif
    }
}

void mpsl_fem_extended_pin_to_mpsl_fem_pin(uint8_t pin_num, mpsl_fem_pin_t* p_fem_pin)
{
    const char *gpio_lbl = NULL;

    pin_num_to_gpio_lbl_and_pin(pin_num, &gpio_lbl, &p_fem_pin->port_pin);

    if (strcmp(gpio_lbl, DT_LABEL(DT_NODELABEL(gpio0))) == 0) {
        p_fem_pin->p_port =  (NRF_GPIO_Type *) DT_REG_ADDR(DT_NODELABEL(gpio0));
        p_fem_pin->port_no  = 0;
        return;
    }
    if (strcmp(gpio_lbl, DT_LABEL(DT_NODELABEL(gpio1))) == 0) {
        p_fem_pin->p_port =  (NRF_GPIO_Type *) DT_REG_ADDR(DT_NODELABEL(gpio1));
        p_fem_pin->port_no  = 1;
        return;
    }

    __ASSERT(false, "Unknown GPIO port DT label");
}

#endif /* defined(CONFIG_MPSL_FEM_PIN_FORWARDER) */
