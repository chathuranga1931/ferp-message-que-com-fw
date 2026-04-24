// pal_mac_crypto.cpp
//
// Simulator implementation of pal_crypto.h using CommonCrypto (macOS built-in).
//
// CommonCrypto is part of macOS — no extra dependencies needed.
// SHA256, Base64 encode/decode, and hex conversion are all used by
// cube_sphere_api.cpp during the device registration challenge-response.

#include "pal_crypto.h"

#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// SHA256
// ---------------------------------------------------------------------------

int32_t pal_crypto_sha256(const uint8_t *input, size_t input_len, uint8_t *output)
{
    if (!input || !output) return -1;
    CC_SHA256(input, (CC_LONG)input_len, output);
    return 0;
}

// ---------------------------------------------------------------------------
// MD5 (not used by cube_sphere, but part of the interface)
// ---------------------------------------------------------------------------

int32_t pal_crypto_md5(const uint8_t *input, size_t input_len, uint8_t *output)
{
    if (!input || !output) return -1;
    CC_MD5(input, (CC_LONG)input_len, output);
    return 0;
}

// ---------------------------------------------------------------------------
// Base64 encode
// ---------------------------------------------------------------------------

static const char k_b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int32_t pal_crypto_base64_encode(const uint8_t *input, size_t input_len,
                                  char *output, size_t output_len)
{
    if (!input || !output) return -1;

    size_t required = ((input_len + 2) / 3) * 4 + 1;
    if (output_len < required) return -1;

    size_t out_i = 0;
    for (size_t i = 0; i < input_len; i += 3) {
        uint32_t b = (uint32_t)input[i] << 16;
        if (i + 1 < input_len) b |= (uint32_t)input[i + 1] << 8;
        if (i + 2 < input_len) b |= (uint32_t)input[i + 2];

        output[out_i++] = k_b64_table[(b >> 18) & 0x3F];
        output[out_i++] = k_b64_table[(b >> 12) & 0x3F];
        output[out_i++] = (i + 1 < input_len) ? k_b64_table[(b >> 6) & 0x3F] : '=';
        output[out_i++] = (i + 2 < input_len) ? k_b64_table[b & 0x3F] : '=';
    }
    output[out_i] = '\0';
    return (int32_t)out_i;
}

// ---------------------------------------------------------------------------
// Base64 decode
// ---------------------------------------------------------------------------

static int _b64_char_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int32_t pal_crypto_base64_decode(const char *input, size_t input_len,
                                  uint8_t *output, size_t output_len)
{
    if (!input || !output) return -1;
    if (input_len == 0) input_len = strlen(input);

    size_t out_i = 0;
    for (size_t i = 0; i + 3 < input_len; i += 4) {
        int v0 = _b64_char_val(input[i]);
        int v1 = _b64_char_val(input[i + 1]);
        int v2 = _b64_char_val(input[i + 2]);
        int v3 = _b64_char_val(input[i + 3]);

        if (v0 < 0 || v1 < 0) break;

        if (out_i < output_len) output[out_i++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (v2 >= 0 && out_i < output_len) output[out_i++] = (uint8_t)((v1 << 4) | (v2 >> 2));
        if (v3 >= 0 && out_i < output_len) output[out_i++] = (uint8_t)((v2 << 6) | v3);
    }
    return (int32_t)out_i;
}

size_t pal_crypto_base64_encode_len(size_t input_len)
{
    return ((input_len + 2) / 3) * 4 + 1;
}

size_t pal_crypto_base64_decode_len(size_t input_len)
{
    return (input_len * 3) / 4 + 1;
}

// ---------------------------------------------------------------------------
// Hex utilities
// ---------------------------------------------------------------------------

int32_t pal_crypto_bin_to_hex(const uint8_t *input, size_t input_len,
                               char *output, size_t output_len)
{
    if (!input || !output) return -1;
    if (output_len < input_len * 2 + 1) return -1;

    for (size_t i = 0; i < input_len; i++) {
        snprintf(output + i * 2, 3, "%02x", input[i]);
    }
    return (int32_t)(input_len * 2);
}

int32_t pal_crypto_hex_to_bin(const char *input, size_t input_len,
                               uint8_t *output, size_t output_len)
{
    if (!input || !output) return -1;
    if (input_len == 0) input_len = strlen(input);

    size_t bytes = input_len / 2;
    if (output_len < bytes) return -1;

    for (size_t i = 0; i < bytes; i++) {
        unsigned int byte_val = 0;
        sscanf(input + i * 2, "%2x", &byte_val);
        output[i] = (uint8_t)byte_val;
    }
    return (int32_t)bytes;
}
