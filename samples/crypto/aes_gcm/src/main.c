/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <psa/crypto.h>
#include <psa/crypto_extra.h>

#ifdef CONFIG_BUILD_WITH_TFM
#include <tfm_ns_interface.h>
#endif

#define APP_SUCCESS             (0)
#define APP_ERROR               (-1)
#define APP_SUCCESS_MESSAGE "Example finished successfully!"
#define APP_ERROR_MESSAGE "Example exited with error!"

#define PRINT_HEX(p_label, p_text, len)				  \
	({							  \
		LOG_INF("---- %s (len: %u): ----", p_label, len); \
		LOG_HEXDUMP_INF(p_text, len, "Content:");	  \
		LOG_INF("---- %s end  ----", p_label);		  \
	})

LOG_MODULE_REGISTER(aes_gcm, LOG_LEVEL_DBG);

/* ====================================================================== */
/*			Global variables/defines for the AES GCM mode example		  */

#define NRF_CRYPTO_EXAMPLE_AES_MAX_TEXT_SIZE (100)
#define NRF_CRYPTO_EXAMPLE_AES_BLOCK_SIZE (16)
#define NRF_CRYPTO_EXAMPLE_AES_IV_SIZE (12)
#define NRF_CRYPTO_EXAMPLE_AES_ADDITIONAL_SIZE (35)
#define NRF_CRYPTO_EXAMPLE_AES_GCM_TAG_LENGTH (16)

/* AES sample IV, DO NOT USE IN PRODUCTION */
static uint8_t m_iv[NRF_CRYPTO_EXAMPLE_AES_IV_SIZE]
= {0x6f, 0x5b, 0x11, 0xfd, 0xcf, 0xe6, 0x72, 0xca, 0x44, 0xc1, 0x35, 0xc8,};


/* Below text is used as plaintext for encryption/decryption */
static uint8_t m_plain_text[] = {
	"Example string to demonstrate basic usage of AES GCM mode."
};

/* Below text is used as additional data for authentication */
static uint8_t m_additional_data[] = {
	// "Example string of additional data"
	// "test_aad"
	0x83, 0x67, 0x45, 0x6e, 0x63, 0x72, 0x79, 0x70, 0x74, 0x43, 0xa1, 0x01, 0x03, 0x40
};

static uint8_t m_key[] = {
0x98, 0xfc, 0x36, 0xbc, 0x86, 0xb8, 0x28, 0xfd, 0xab, 0x2c, 0x5e, 0x5f, 0x3f, 0xc2, 0x1e, 0xbb,
0x96, 0xf3, 0x23, 0xe0, 0xc6, 0x5d, 0xb0, 0xcd, 0x4a, 0xe9, 0xe8, 0xce, 0xd3, 0x52, 0x05, 0xaa,
};

static uint8_t m_encrypted_text[]
= {
/* encrypted */
 0xfa, 0xd8, 0x80, 0x4f,
0x24, 0x83, 0xcb, 0x1b, 0x06, 0x8f, 0xa2, 0x3d, 0x45, 0x97, 0xcf, 0x10, 0xbc, 0x2b, 0xe7, 0x99,
0x6b, 0x18, 0xbe, 0x96, 0x39, 0x96, 0x1d, 0xc9, 0x78, 0x60, 0x07, 0x0f, 0x8c, 0x4e, 0xfe, 0x3e,
0xe3, 0x07, 0xc6, 0xc2, 0xad, 0xcf, 0xa3, 0xcb, 0x16, 0x1c, 0x65, 0x7b, 0x04, 0x28, 0x19, 0x22,
0xdf, 0x9d, 0x16, 0x24, 0x3e, 0x06, 0x5c,
/* tag */
0xb5, 0x50, 0x75, 0xe5,
0x06, 0x07, 0xbc, 0xfe, 0x1f, 0x0c, 0x77, 0x9f, 0x5a, 0x13, 0x8c, 0x3e,
};

static uint8_t m_decrypted_text[NRF_CRYPTO_EXAMPLE_AES_MAX_TEXT_SIZE];

static psa_key_id_t key_id;
/* ====================================================================== */

int crypto_init(void)
{
	psa_status_t status;

	/* Initialize PSA Crypto */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		return APP_ERROR;
	}

	return APP_SUCCESS;
}

int crypto_finish(void)
{
	psa_status_t status;

	/* Destroy the key handle */
	status = psa_destroy_key(key_id);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_destroy_key failed! (Error: %d)", status);
		return APP_ERROR;
	}

	return APP_SUCCESS;
}

int generate_key(void)
{
	psa_status_t status;

	LOG_INF("Generating random AES key...");

	/* Configure the key attributes */
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_GCM);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&key_attributes, 256);

// 	/* Generate a random key. The key is not exposed to the application,
// 	 * we can use it to encrypt/decrypt using the key handle
// 	 */
// 	status = psa_generate_key(&key_attributes, &key_id);
	status = psa_import_key(&key_attributes,
                            m_key,
                            sizeof(m_key),
                            &key_id);

	if (status != PSA_SUCCESS) {
		LOG_INF("psa_import failed! (Error: %d)", status);
		return APP_ERROR;
	}

// 	/* After the key handle is acquired the attributes are not needed */
// 	psa_reset_key_attributes(&key_attributes);

	LOG_INF("AES key generated successfully!");

	return 0;
}

// int encrypt_aes_gcm(void)
// {
// 	uint32_t output_len;
// 	psa_status_t status;

// 	LOG_INF("Encrypting using AES GCM MODE...");

// 	/* Generate a random IV */
// 	status = psa_generate_random(m_iv, NRF_CRYPTO_EXAMPLE_AES_IV_SIZE);
// 	if (status != PSA_SUCCESS) {
// 		LOG_INF("psa_generate_random failed! (Error: %d)", status);
// 		return APP_ERROR;
// 	}

// 	/* Encrypt the plaintext and create the authentication tag */
// 	status = psa_aead_encrypt(key_id,
// 				  PSA_ALG_GCM,
// 				  m_iv,
// 				  sizeof(m_iv),
// 				  m_additional_data,
// 				  sizeof(m_additional_data),
// 				  m_plain_text,
// 				  sizeof(m_plain_text),
// 				  m_encrypted_text,
// 				  sizeof(m_encrypted_text),
// 				  &output_len);
// 	if (status != PSA_SUCCESS) {
// 		LOG_INF("psa_aead_encrypt failed! (Error: %d)", status);
// 		return APP_ERROR;
// 	}

// 	LOG_INF("Encryption successful!");
// 	PRINT_HEX("IV", m_iv, sizeof(m_iv));
// 	PRINT_HEX("Additional data", m_additional_data, sizeof(m_additional_data));
// 	PRINT_HEX("Plaintext", m_plain_text, sizeof(m_plain_text));
// 	PRINT_HEX("Encrypted text", m_encrypted_text, sizeof(m_encrypted_text));

// 	return APP_SUCCESS;
// }

int decrypt_aes_gcm(void)
{
	uint32_t output_len;
	psa_status_t status;

	LOG_INF("Decrypting using AES GCM MODE...");

	/* Decrypt and authenticate the encrypted data */
	status = psa_aead_decrypt(key_id,
				  PSA_ALG_GCM,
				  m_iv,
				  sizeof(m_iv),
				  m_additional_data,
				  sizeof(m_additional_data) - 1, // ARHA - WAZNE
				  m_encrypted_text,
				  sizeof(m_encrypted_text),
				  m_decrypted_text,
				  sizeof(m_decrypted_text),
				  &output_len);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_aead_decrypt failed! (Error: %d)", status);
		return APP_ERROR;
	}

	PRINT_HEX("Decrypted text", m_decrypted_text, sizeof(m_decrypted_text));

	/* Check the validity of the decryption */
	if (memcmp(m_decrypted_text, m_plain_text, sizeof(m_plain_text) - 1) != 0) {
		LOG_INF("Error: Decrypted text doesn't match the plaintext");
		return APP_ERROR;
	}

	LOG_INF("Decryption and authentication successful!");

	return APP_SUCCESS;
}

int main(void)
{
	int status;

	LOG_INF("Starting AES-GCM example...");

	status = crypto_init();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	status = generate_key();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	PRINT_HEX("IV", m_iv, sizeof(m_iv));
	PRINT_HEX("Additional data", m_additional_data, sizeof(m_additional_data));
	PRINT_HEX("Plaintext", m_plain_text, sizeof(m_plain_text));
	PRINT_HEX("Encrypted text", m_encrypted_text, sizeof(m_encrypted_text));
	// status = encrypt_aes_gcm();
	// if (status != APP_SUCCESS) {
	// 	LOG_INF(APP_ERROR_MESSAGE);
	// 	return APP_ERROR;
	// }

	status = decrypt_aes_gcm();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	status = crypto_finish();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	LOG_INF(APP_SUCCESS_MESSAGE);

	return APP_SUCCESS;
}
