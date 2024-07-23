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

static uint8_t m_kek_key[] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static uint8_t encrypted_cek[] = {
0x28, 0xC9, 0xF4, 0x04, 0xC4, 0xB8, 0x10, 0xF4, 0xCB, 0xCC, 0xB3, 0x5C, 0xFB, 0x87, 0xF8, 0x26, 0x3F, 0x57, 0x86, 0xE2, 0xD8, 0x0E, 0xD3, 0x26,
0xCB, 0xC7, 0xF0, 0xE7, 0x1A, 0x99, 0xF4, 0x3B, 0xFB, 0x98, 0x8B, 0x9B, 0x7A, 0x02, 0xDD, 0x21,
};

static psa_key_id_t m_kek_key_id;

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

psa_status_t arha_key_unwrap(psa_key_id_t kek_key_id, psa_algorithm_t key_enc_alg, uint8_t* wrapped_cek,
		    size_t cek_bits, uint8_t* unwrapped_cek)
{

	psa_status_t status = PSA_SUCCESS;
	/* TODO: verify wrapped_cek is of correct length */

	/* The variable names are matching the names from RFC3394, chapter 2.2.2 (second algorithm variant) */
	size_t n = cek_bits / 64;
	uint8_t t = 0; /* Maximum value for t is 24 -> can be uint8_t */
	uint8_t A[8] = {0}; /* A is a 64 bit value*/
	uint8_t B[16] = {0};
	/* 5: 256/64 + 1 - 256 is the maximum key size in bits,the algorithm is working on 64 bits values,
	   need n + 1 values.
	   An additional 1 is added to allow indexing R from 1 as in the specification.
	   */
	uint8_t R[5+1][8] = {{0}};

	uint8_t temp[16] = {0};
	// uint8_t temp_buf[16] = {0};
	size_t temp_output_length = 0;
	const static uint8_t kw_iv[8] = {0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6};

	/* Initialize the variables */
	memcpy(A, wrapped_cek, 8);

	for (size_t i = 1; i <= 5; i++) {
		wrapped_cek += 8;
		memcpy(R[i], wrapped_cek, 8);
	}

	/* Compute intermediate values */
	for (int j = 5; j >= 0; j--) {
		for (size_t i = n; i > 0; i--) {
			t = n * j + i;
			A[7] = A[7] ^ t; /* Only the LSB will be affected by the xor, as t is always smaller than 256 */
			memcpy(temp, A, 8);
			memcpy(temp + 8, R[i], 8);
			status = psa_cipher_decrypt(kek_key_id,
                                key_enc_alg,
                                temp,
                                sizeof(temp),
                                B,
                                sizeof(B),
                                &temp_output_length);

			if (status != PSA_SUCCESS) {
				return status;
			}

			if (temp_output_length != sizeof(B)) {
				return PSA_ERROR_GENERIC_ERROR;
			}

			memcpy(A, B, 8);
			memcpy(R[i], B+8, 8);
		}
	}

	if (memcmp(A, kw_iv, 8) != 0){
		return PSA_ERROR_INVALID_SIGNATURE;
	}

	for (size_t i = 0; i < n; i++) {
		memcpy(unwrapped_cek + 8*i, R[i+1], 8);
	}

	return PSA_SUCCESS;
}

int import_kek(void)
{
	psa_status_t status;

	LOG_INF("importing kek");

	/* Configure the key attributes */
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	// psa_set_key_algorithm(&key_attributes, PSA_ALG_CCM_STAR_NO_TAG);
	// psa_set_key_algorithm(&key_attributes, PSA_ALG_CBC_NO_PADDING);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECB_NO_PADDING);
	// psa_set_key_algorithm(&key_attributes, PSA_ALG_CTR);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&key_attributes, 256);

	status = psa_import_key(&key_attributes,
                            m_kek_key,
                            sizeof(m_kek_key),
                            &m_kek_key_id);

	if (status != PSA_SUCCESS) {
		LOG_INF("psa_import failed! (Error: %d)", status);
		return APP_ERROR;
	}

	LOG_INF("KEK imported successfully!");

	return 0;
}

int unwrap_test(void)
{
	import_kek();
	uint8_t cek[32] = {0};
	psa_status_t status = PSA_SUCCESS;
	// status = arha_key_unwrap(m_kek_key_id, PSA_ALG_CBC_NO_PADDING, encrypted_cek, 256, cek);
	status = arha_key_unwrap(m_kek_key_id, PSA_ALG_ECB_NO_PADDING, encrypted_cek, 256, cek);
	// status = arha_key_unwrap(m_kek_key_id, PSA_ALG_CTR, encrypted_cek, 256, cek);
	if (status != PSA_SUCCESS) {
		LOG_INF("arha_key_unwrap failed! (Error: %d)", status);
		return APP_ERROR;
	}
	LOG_INF("arha_key_unwrap successful!");
	PRINT_HEX("CEK", cek, sizeof(cek));
	psa_destroy_key(m_kek_key_id);
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

	unwrap_test();
	// status = generate_key();
	// if (status != APP_SUCCESS) {
	// 	LOG_INF(APP_ERROR_MESSAGE);
	// 	return APP_ERROR;
	// }

	// PRINT_HEX("IV", m_iv, sizeof(m_iv));
	// PRINT_HEX("Additional data", m_additional_data, sizeof(m_additional_data));
	// PRINT_HEX("Plaintext", m_plain_text, sizeof(m_plain_text));
	// PRINT_HEX("Encrypted text", m_encrypted_text, sizeof(m_encrypted_text));
	// // status = encrypt_aes_gcm();
	// // if (status != APP_SUCCESS) {
	// // 	LOG_INF(APP_ERROR_MESSAGE);
	// // 	return APP_ERROR;
	// // }

	// status = decrypt_aes_gcm();
	// if (status != APP_SUCCESS) {
	// 	LOG_INF(APP_ERROR_MESSAGE);
	// 	return APP_ERROR;
	// }

	// status = crypto_finish();
	// if (status != APP_SUCCESS) {
	// 	LOG_INF(APP_ERROR_MESSAGE);
	// 	return APP_ERROR;
	// }

	LOG_INF(APP_SUCCESS_MESSAGE);

	return APP_SUCCESS;
}
