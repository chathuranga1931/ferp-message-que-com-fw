// mqtt_auth.h
//
// Application-level MQTT authentication for FERP devices.
//
// Protects against MITM on public MQTT brokers by requiring every inbound
// command envelope to carry a keyed hash of its sequence number.
//
// Protocol extension (cmd envelope):
//   {
//     "seq"     : <uint32>,
//     "msg"     : "<msg_name>",
//     "data"    : { ... },
//     "hop_idx" : <0-15>,          // index into the shared key table
//     "hash"    : "<16-char hex>"  // mqtt_auth_hash(keys[hop_idx], seq)
//   }
//
// Algorithm: splitmix64-style keyed hash (no library dependencies,
// identical output on ESP32 C and CPython).
//
// Key table: 16 × 64-bit constants shared between firmware and tool.
// MUST be kept in sync with tools/ferp-device-tool/mqtt_auth.py.

#pragma once

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define MQTT_AUTH_KEY_COUNT  16

// ---------------------------------------------------------------------------
// Shared key table
// ---------------------------------------------------------------------------

static const uint64_t k_mqtt_auth_keys[MQTT_AUTH_KEY_COUNT] = {
    UINT64_C(0xA3F2E1B0C4D5E6F7), UINT64_C(0x1B2C3D4E5F607182),
    UINT64_C(0x9A8B7C6D5E4F3021), UINT64_C(0xFEDCBA9876543210),
    UINT64_C(0x0F1E2D3C4B5A6978), UINT64_C(0x7E6F5D4C3B2A1908),
    UINT64_C(0xC0FFEE1234567890), UINT64_C(0xDEADBEEF0BADF00D),
    UINT64_C(0x0123456789ABCDEF), UINT64_C(0xFEDCBA9812345678),
    UINT64_C(0x2468ACE013579BDF), UINT64_C(0x1357924680BDFACE),
    UINT64_C(0xABCDEF0123456789), UINT64_C(0x9876543210FEDCBA),
    UINT64_C(0x5A5A5A5A5A5A5A5A), UINT64_C(0xA5A5A5A5A5A5A5A5),
};

// ---------------------------------------------------------------------------
// Hash function
// ---------------------------------------------------------------------------

/**
 * Compute a 64-bit authentication hash from a shared key and a 32-bit
 * sequence number using a splitmix64-style finalizer.
 *
 * This is intentionally simple — zero library dependencies, same result
 * on any 64-bit-capable platform (ESP32, CPython, PyPy).
 */
static inline uint64_t mqtt_auth_hash(uint64_t key, uint32_t seq)
{
    uint64_t h = key ^ ((uint64_t)seq * UINT64_C(0x9e3779b97f4a7c15));
    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    h *= UINT64_C(0x94d049bb133111eb);
    h ^= h >> 31;
    return h;
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

/**
 * Verify an inbound MQTT cmd envelope.
 *
 * @param hop_idx  Key index from the "hop_idx" JSON field (0–15).
 * @param hash_hex 16-char lowercase hex string from the "hash" JSON field.
 * @param seq      Sequence number from the "seq" JSON field.
 * @return true if hop_idx is in range and the computed hash matches hash_hex.
 */
static inline bool mqtt_auth_verify(uint8_t hop_idx, const char *hash_hex, uint32_t seq)
{
    if (!hash_hex || strlen(hash_hex) != 16) return false;
    if (hop_idx >= MQTT_AUTH_KEY_COUNT) return false;

    // Parse the 16-char hex string into a uint64
    uint64_t received = 0;
    for (int i = 0; i < 16; i++) {
        char c = hash_hex[i];
        uint8_t nibble;
        if      (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
        else return false;
        received = (received << 4) | nibble;
    }

    uint64_t expected = mqtt_auth_hash(k_mqtt_auth_keys[hop_idx], seq);
    return expected == received;
}
