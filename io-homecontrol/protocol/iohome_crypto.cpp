/**
 * @file iohome_crypto.cpp
 * @brief io-homecontrol Cryptographic Functions Implementation
 * @author iown-homecontrol project
 */

#include "iohome_crypto.h"
#include <string.h>

// https://mbed-tls.readthedocs.io/en/latest/getting_started/psa/
#include "psa/crypto.h"

#include "esp_log.h"
static const char *TAG = "io-crypto";

namespace iohome
{
  namespace crypto
  {

    // ============================================================================
    // Checksum Functions (for IV construction)
    // ============================================================================

    void compute_checksum(uint8_t frame_byte, uint8_t &chksum1, uint8_t &chksum2)
    {
      uint8_t tmpchksum = frame_byte ^ chksum2;
      chksum2 = ((chksum1 & 0x7F) << 1) & 0xFF;

      if ((chksum1 & 0x80) == 0)
      {
        if (tmpchksum >= 128)
        {
          chksum2 |= 1;
        }
        chksum1 = chksum2;
        chksum2 = (tmpchksum << 1) & 0xFF;
        return;
      }

      if (tmpchksum >= 128)
      {
        chksum2 |= 1;
      }

      chksum1 = chksum2 ^ 0x55;
      chksum2 = ((tmpchksum << 1) ^ 0x5B) & 0xFF;
    }

    // ============================================================================
    // Initial Value (IV) Construction
    // ============================================================================

    void generate_challenge(uint8_t challenge_out[HMAC_SIZE])
    {
      // Generate random 6-byte challenge
      psa_status_t status = psa_generate_random(challenge_out, HMAC_SIZE);
      if (status != PSA_SUCCESS)
      {
        ESP_LOGE(TAG, "generate_challenge - Failed to generate random! (%d)", status);
      }
    }

    void construct_iv_2w(
        const uint8_t *frame_data,
        size_t data_len,
        const uint8_t challenge[HMAC_SIZE],
        uint8_t iv_out[IV_SIZE])
    {
      // Initialize IV
      memset(iv_out, 0, IV_SIZE);

      // Initialize checksums
      uint8_t chksum1 = 0;
      uint8_t chksum2 = 0;

      // Process frame data
      for (size_t i = 0; i < data_len; i++)
      {
        compute_checksum(frame_data[i], chksum1, chksum2);
        if (i < 8)
        {
          iv_out[i] = frame_data[i];
        }
      }

      // Pad bytes 0-7 with 0x55 if data is shorter than 8 bytes
      for (size_t i = data_len; i < 8; i++)
      {
        iv_out[i] = IV_PADDING;
      }

      // Set checksums (bytes 8-9)
      iv_out[8] = chksum1;
      iv_out[9] = chksum2;

      // Set challenge (bytes 10-15)
      memcpy(&iv_out[10], challenge, HMAC_SIZE);
    }

    // ============================================================================
    // AES-128 Encryption/Decryption
    // ============================================================================

    bool aes128_encrypt(
        const uint8_t input[AES_BLOCK_SIZE],
        const uint8_t key[AES_KEY_SIZE],
        uint8_t output[AES_BLOCK_SIZE])
    {
      psa_status_t status;
      psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
      psa_algorithm_t alg = PSA_ALG_ECB_NO_PADDING;
      size_t output_length = 0;
      psa_key_id_t key_id;
      // Import key
      psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
      psa_set_key_algorithm(&attributes, alg);
      psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
      psa_set_key_bits(&attributes, 8 * AES_KEY_SIZE);
      status = psa_import_key(&attributes, key, AES_KEY_SIZE, &key_id);
      if (status != PSA_SUCCESS)
      {
        ESP_LOGE(TAG, "aes128_encrypt - Failed to import a key (%d)", status);
        return false;
      }
      psa_reset_key_attributes(&attributes);
      // Cipher
      status = psa_cipher_encrypt(key_id, PSA_ALG_ECB_NO_PADDING, input, AES_BLOCK_SIZE, output, AES_BLOCK_SIZE, &output_length);
      psa_destroy_key(key_id); // don't forget to destroy the key!
      if (status != PSA_SUCCESS)
      {
        ESP_LOGE(TAG, "aes128_encrypt - Failed to begin cipher operation (%d)", status);
        return false;
      }
      return true;
    }

    bool aes128_decrypt(
        const uint8_t input[AES_BLOCK_SIZE],
        const uint8_t key[AES_KEY_SIZE],
        uint8_t output[AES_BLOCK_SIZE])
    {
      psa_status_t status;
      psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
      psa_algorithm_t alg = PSA_ALG_ECB_NO_PADDING;
      size_t output_length = 0;
      psa_key_id_t key_id;
      // Import key
      psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
      psa_set_key_algorithm(&attributes, alg);
      psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
      psa_set_key_bits(&attributes, 8 * AES_KEY_SIZE);
      status = psa_import_key(&attributes, key, AES_KEY_SIZE, &key_id);
      if (status != PSA_SUCCESS)
      {
        ESP_LOGE(TAG, "aes128_decrypt - Failed to import a key (%d)", status);
        return false;
      }
      psa_reset_key_attributes(&attributes);
      // Cipher
      status = psa_cipher_decrypt(key_id, PSA_ALG_ECB_NO_PADDING, input, AES_BLOCK_SIZE, output, AES_BLOCK_SIZE, &output_length);
      psa_destroy_key(key_id); // don't forget to destroy the key!
      if (status != PSA_SUCCESS)
      {
        ESP_LOGE(TAG, "aes128_decrypt - Failed to begin cipher operation (%d)", status);
        return false;
      }
      return true;
    }

    // ============================================================================
    // Key Encryption (for pairing)
    // ============================================================================

    bool crypt_2w_key(
        const uint8_t *frame_data,
        size_t data_len,
        const uint8_t challenge[HMAC_SIZE],
        const uint8_t input[AES_KEY_SIZE],
        uint8_t output[AES_KEY_SIZE])
    {
      // Construct IV with challenge
      uint8_t iv[IV_SIZE];
      construct_iv_2w(frame_data, data_len, challenge, iv);

      // Encrypt IV with transfer key
      uint8_t encrypted_iv[AES_BLOCK_SIZE];
      if (!aes128_encrypt(iv, TRANSFER_KEY, encrypted_iv))
      {
        return false;
      }

      // XOR input buffer with encrypted IV
      for (int i = 0; i < AES_KEY_SIZE; i++)
      {
        output[i] = input[i] ^ encrypted_iv[i];
      }

      return true;
    }

    // ============================================================================
    // HMAC/MAC
    // ============================================================================

    bool create_2w_hmac(
        const uint8_t *frame_data,
        size_t data_len,
        const uint8_t challenge[HMAC_SIZE],
        const uint8_t system_key[AES_KEY_SIZE],
        uint8_t hmac_out[HMAC_SIZE])
    {
      // Construct IV
      uint8_t iv[IV_SIZE];
      construct_iv_2w(frame_data, data_len, challenge, iv);

      // Encrypt IV with system key
      uint8_t encrypted_iv[AES_BLOCK_SIZE];
      if (!aes128_encrypt(iv, system_key, encrypted_iv))
      {
        return false;
      }

      // Truncate to 6 bytes for HMAC
      memcpy(hmac_out, encrypted_iv, HMAC_SIZE);

      return true;
    }

    bool verify_hmac(
        const uint8_t *frame_data,
        size_t data_len,
        const uint8_t received_hmac[HMAC_SIZE],
        const uint8_t *challenge,
        const uint8_t system_key[AES_KEY_SIZE])
    {
      uint8_t calculated_hmac[HMAC_SIZE];
      bool success;

      success = create_2w_hmac(frame_data, data_len, challenge, system_key, calculated_hmac);

      if (!success)
      {
        return false;
      }

      // Compare HMACs (constant-time comparison for security)
      uint8_t diff = 0;
      for (int i = 0; i < HMAC_SIZE; i++)
      {
        diff |= calculated_hmac[i] ^ received_hmac[i];
      }

      return (diff == 0);
    }

  } // namespace crypto
} // namespace iohome
