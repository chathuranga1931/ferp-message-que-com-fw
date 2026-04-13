/**
 * @file pal_esp_idf_crypto.cpp
 * @brief ESP-IDF implementation of cryptographic PAL
 */

#include "pal_crypto.h"
#include "pal_logger.h"

#include "mbedtls/sha256.h"
#include "mbedtls/md5.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <ctype.h>

#define __TAG__ "PAL_CRYP"

#define CRYP_DEBUG_LOG_EN      LOG_DIS

int32_t pal_crypto_sha256(const uint8_t* input, 
                          size_t input_len, 
                          uint8_t* output)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    
    int ret = mbedtls_sha256_starts(&ctx, 0); // 0 = SHA256 (not SHA224)
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "SHA256 start failed");
        return -1;
    }
    
    ret = mbedtls_sha256_update(&ctx, input, input_len);
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "SHA256 update failed");
        return -1;
    }
    
    ret = mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
    
    if (ret != 0) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "SHA256 finish failed");
        return -1;
    }
    
    return 0;
}

int32_t pal_crypto_md5(const uint8_t* input, 
                       size_t input_len, 
                       uint8_t* output)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    
    int ret = mbedtls_md5_starts(&ctx);
    if (ret != 0) {
        mbedtls_md5_free(&ctx);
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "MD5 start failed");
        return -1;
    }
    
    ret = mbedtls_md5_update(&ctx, input, input_len);
    if (ret != 0) {
        mbedtls_md5_free(&ctx);
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "MD5 update failed");
        return -1;
    }
    
    ret = mbedtls_md5_finish(&ctx, output);
    mbedtls_md5_free(&ctx);
    
    if (ret != 0) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "MD5 finish failed");
        return -1;
    }
    
    return 0;
}

int32_t pal_crypto_base64_encode(const uint8_t* input,
                                  size_t input_len,
                                  char* output,
                                  size_t output_len)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char*)output, output_len, &olen, input, input_len);
    
    if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Base64 encode buffer too small");
        return -1;
    } else if (ret != 0) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Base64 encode failed");
        return -1;
    }
    
    return olen;
}

int32_t pal_crypto_base64_decode(const char* input,
                                  size_t input_len,
                                  uint8_t* output,
                                  size_t output_len)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    // Auto-detect input length if needed
    if (input_len == 0) {
        input_len = strlen(input);
    }

    size_t olen = 0;
    int ret = mbedtls_base64_decode(output, output_len, &olen, (const unsigned char*)input, input_len);
    
    if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Base64 decode buffer too small");
        return -1;
    } else if (ret == MBEDTLS_ERR_BASE64_INVALID_CHARACTER) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Base64 decode invalid character");
        return -1;
    } else if (ret != 0) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Base64 decode failed");
        return -1;
    }
    
    return olen;
}

size_t pal_crypto_base64_encode_len(size_t input_len)
{
    // Base64 encoding: 4 chars for every 3 bytes, plus null terminator
    return ((input_len + 2) / 3) * 4 + 1;
}

size_t pal_crypto_base64_decode_len(size_t input_len)
{
    // Base64 decoding: 3 bytes for every 4 chars (approximate)
    return (input_len * 3) / 4 + 1;
}

int32_t pal_crypto_bin_to_hex(const uint8_t* input,
                               size_t input_len,
                               char* output,
                               size_t output_len)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    // Check if output buffer is large enough
    if (output_len < (input_len * 2 + 1)) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Hex encode buffer too small");
        return -1;
    }

    for (size_t i = 0; i < input_len; i++) {
        sprintf(output + (i * 2), "%02x", input[i]);
    }
    output[input_len * 2] = '\0';
    
    return input_len * 2;
}

int32_t pal_crypto_hex_to_bin(const char* input,
                               size_t input_len,
                               uint8_t* output,
                               size_t output_len)
{
    if (input == NULL || output == NULL) {
        return -1;
    }

    // Auto-detect input length if needed
    if (input_len == 0) {
        input_len = strlen(input);
    }

    // Hex string must have even length
    if (input_len % 2 != 0) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Hex string must have even length");
        return -1;
    }

    size_t bin_len = input_len / 2;
    if (output_len < bin_len) {
        LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Hex decode buffer too small");
        return -1;
    }

    for (size_t i = 0; i < bin_len; i++) {
        char hex[3] = {input[i * 2], input[i * 2 + 1], '\0'};
        
        // Check for valid hex characters
        if (!isxdigit(hex[0]) || !isxdigit(hex[1])) {
            LOG_MSG_ERROR(CRYP_DEBUG_LOG_EN, "Invalid hex character");
            return -1;
        }
        
        output[i] = (uint8_t)strtol(hex, NULL, 16);
    }
    
    return bin_len;
}
