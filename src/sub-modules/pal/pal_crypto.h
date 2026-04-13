/**
 * @file pal_crypto.h
 * @brief Platform Abstraction Layer for Cryptographic operations
 * 
 * This header provides a platform-independent interface for cryptographic
 * functions including hashing, encoding, and other security primitives.
 */

#ifndef PAL_CRYPTO_H
#define PAL_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define PAL_SHA256_DIGEST_LENGTH    32      /**< SHA256 hash length in bytes */
#define PAL_MD5_DIGEST_LENGTH       16      /**< MD5 hash length in bytes */

// ============================================================================
// Hashing Functions
// ============================================================================

/**
 * @brief Calculate SHA256 hash
 * 
 * Computes the SHA256 hash of the input data.
 * 
 * @param input Input data to hash
 * @param input_len Length of input data
 * @param output Buffer to store hash (must be at least PAL_SHA256_DIGEST_LENGTH bytes)
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_crypto_sha256(const uint8_t* input, 
                          size_t input_len, 
                          uint8_t* output);

/**
 * @brief Calculate MD5 hash
 * 
 * Computes the MD5 hash of the input data.
 * 
 * @param input Input data to hash
 * @param input_len Length of input data
 * @param output Buffer to store hash (must be at least PAL_MD5_DIGEST_LENGTH bytes)
 * @return int32_t 0 on success, negative error code on failure
 */
int32_t pal_crypto_md5(const uint8_t* input, 
                       size_t input_len, 
                       uint8_t* output);

// ============================================================================
// Encoding Functions
// ============================================================================

/**
 * @brief Encode data to Base64
 * 
 * Encodes binary data to Base64 string format.
 * 
 * @param input Input data to encode
 * @param input_len Length of input data
 * @param output Buffer to store Base64 string (includes null terminator)
 * @param output_len Length of output buffer (must be at least ((input_len + 2) / 3) * 4 + 1)
 * @return int32_t Number of bytes written (excluding null terminator), negative error code on failure
 */
int32_t pal_crypto_base64_encode(const uint8_t* input,
                                  size_t input_len,
                                  char* output,
                                  size_t output_len);

/**
 * @brief Decode Base64 data
 * 
 * Decodes Base64 string to binary data.
 * 
 * @param input Base64 string to decode
 * @param input_len Length of input string (or 0 to auto-detect with strlen)
 * @param output Buffer to store decoded data
 * @param output_len Length of output buffer (must be at least (input_len * 3) / 4)
 * @return int32_t Number of bytes written, negative error code on failure
 */
int32_t pal_crypto_base64_decode(const char* input,
                                  size_t input_len,
                                  uint8_t* output,
                                  size_t output_len);

/**
 * @brief Calculate required Base64 encode buffer size
 * 
 * Calculates the buffer size needed for Base64 encoding.
 * 
 * @param input_len Length of data to encode
 * @return size_t Required buffer size (including null terminator)
 */
size_t pal_crypto_base64_encode_len(size_t input_len);

/**
 * @brief Calculate required Base64 decode buffer size
 * 
 * Calculates the buffer size needed for Base64 decoding.
 * 
 * @param input_len Length of Base64 string
 * @return size_t Required buffer size
 */
size_t pal_crypto_base64_decode_len(size_t input_len);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert binary data to hex string
 * 
 * Converts binary data to lowercase hexadecimal string representation.
 * 
 * @param input Input binary data
 * @param input_len Length of input data
 * @param output Buffer to store hex string (must be at least input_len * 2 + 1 bytes)
 * @param output_len Length of output buffer
 * @return int32_t Number of characters written (excluding null terminator), negative error code on failure
 */
int32_t pal_crypto_bin_to_hex(const uint8_t* input,
                               size_t input_len,
                               char* output,
                               size_t output_len);

/**
 * @brief Convert hex string to binary data
 * 
 * Converts hexadecimal string to binary data.
 * 
 * @param input Input hex string (case insensitive)
 * @param input_len Length of input string (or 0 to auto-detect with strlen)
 * @param output Buffer to store binary data (must be at least input_len / 2 bytes)
 * @param output_len Length of output buffer
 * @return int32_t Number of bytes written, negative error code on failure
 */
int32_t pal_crypto_hex_to_bin(const char* input,
                               size_t input_len,
                               uint8_t* output,
                               size_t output_len);

#ifdef __cplusplus
}
#endif

#endif // PAL_CRYPTO_H
