/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <stddef.h>

/** @brief Valid SUIT envelope, based on ../root_validate_fail.yaml
 *
 */
const uint8_t manifest_root_validate_fail_buf[] = {
	0xD8, 0x6B, 0xA2, 0x02, 0x58, 0x7A, 0x82, 0x58, 0x24, 0x82, 0x2F, 0x58, 0x20, 0x8D, 0x1E,
	0x66, 0xE8, 0x32, 0x13, 0x16, 0x4E, 0x57, 0x8A, 0x32, 0x3F, 0x2A, 0x25, 0xA8, 0x83, 0x7E,
	0x0F, 0x08, 0x9C, 0xAC, 0x15, 0x90, 0x2A, 0x22, 0x2F, 0x49, 0x31, 0x5A, 0xD5, 0xEE, 0x85,
	0x58, 0x51, 0xD2, 0x84, 0x4A, 0xA2, 0x01, 0x26, 0x04, 0x45, 0x1A, 0x7F, 0xFF, 0xFF, 0xE0,
	0xA0, 0xF6, 0x58, 0x40, 0x39, 0x29, 0x21, 0x56, 0x82, 0x45, 0x4C, 0x93, 0x7D, 0xE2, 0xF3,
	0x5E, 0xD4, 0x7C, 0x25, 0x58, 0xAD, 0x2F, 0x34, 0x8F, 0x06, 0x3D, 0x12, 0x7B, 0x0E, 0x1A,
	0xAF, 0x66, 0x91, 0x6D, 0x13, 0x12, 0x6D, 0x07, 0xDD, 0xC0, 0xA5, 0x23, 0x86, 0x35, 0x3D,
	0x85, 0xA8, 0xC0, 0x86, 0x38, 0x97, 0xA3, 0xF4, 0xD3, 0xFF, 0xAF, 0xC0, 0x17, 0x16, 0x06,
	0x22, 0x40, 0xC2, 0x4A, 0xE9, 0x06, 0x6D, 0xC1, 0x03, 0x58, 0x87, 0xA6, 0x01, 0x01, 0x02,
	0x01, 0x03, 0x58, 0x52, 0xA3, 0x02, 0x81, 0x82, 0x4A, 0x69, 0x43, 0x41, 0x4E, 0x44, 0x5F,
	0x4D, 0x46, 0x53, 0x54, 0x41, 0x00, 0x04, 0x58, 0x3A, 0x82, 0x14, 0xA3, 0x01, 0x50, 0x76,
	0x17, 0xDA, 0xA5, 0x71, 0xFD, 0x5A, 0x85, 0x8F, 0x94, 0xE2, 0x8D, 0x73, 0x5C, 0xE9, 0xF4,
	0x02, 0x50, 0x97, 0x05, 0x48, 0x23, 0x4C, 0x3D, 0x59, 0xA1, 0x89, 0x86, 0xA5, 0x46, 0x60,
	0xA1, 0x4B, 0x0A, 0x18, 0x18, 0x50, 0x9C, 0x1B, 0x1E, 0x37, 0x2C, 0xB4, 0x5C, 0x33, 0x92,
	0xDD, 0x49, 0x56, 0x6B, 0x18, 0x31, 0x93, 0x01, 0xA1, 0x00, 0xA0, 0x07, 0x46, 0x84, 0x0C,
	0x00, 0x18, 0x18, 0x00, 0x09, 0x43, 0x82, 0x0C, 0x00, 0x05, 0x82, 0x4C, 0x6B, 0x49, 0x4E,
	0x53, 0x54, 0x4C, 0x44, 0x5F, 0x4D, 0x46, 0x53, 0x54, 0x50, 0x97, 0x05, 0x48, 0x23, 0x4C,
	0x3D, 0x59, 0xA1, 0x89, 0x86, 0xA5, 0x46, 0x60, 0xA1, 0x4B, 0x0A};

const size_t manifest_root_validate_fail_len = sizeof(manifest_root_validate_fail_buf);
