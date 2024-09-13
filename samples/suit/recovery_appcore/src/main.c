/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>

volatile int test = 0;

int main(void)
{
	while (true)
	{
		test++;
	}
	return 0;
}
