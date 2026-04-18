/**
 * hsys_config_mac.cpp
 *
 * macOS/Linux simulator implementation of hsys_config.h.
 *
 * Replaces the ArduinoJson-based implementation with plain C++17 that
 * works in the native simulator build.  The JSON subset handled is the
 * flat object format produced/consumed by the config module:
 *
 *   {"key":"value","key2":123,"key3":true}
 *
 * No nesting, no arrays — exactly what DeviceConfigs.json contains.
 */

#include "hsys_config.h"
#include "pal_logger.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define __TAG__  "HS_CONF "

#ifndef HSYS_CFG_LOG_EN
#define HSYS_CFG_LOG_EN true
#endif

// ── hsys_config_init ──────────────────────────────────────────────────────────

int32_t hsys_config_init(config_init_t config_init, config_handle_t *config_hndl)
{
    if (!config_hndl)               return CONFIG_NULL;
    if (!config_init.table)         return CONFIG_NULL;
    if (config_init.config_size == 0) return CONFIG_NULL;

    config_hndl->config_size    = config_init.config_size;
    config_hndl->table          = config_init.table;
    config_hndl->is_initialized = true;
    return CONFIG_SUCCESS;
}

// ── hsys_config_convert_to_json ───────────────────────────────────────────────

int32_t hsys_config_convert_to_json(config_handle_t *config_hndl,
                                    char *json_buffer, size_t buffer_size,
                                    size_t *json_length)
{
    if (!config_hndl)                  return CONFIG_NULL;
    if (!config_hndl->is_initialized)  return CONFIG_UNINTIALIZED;
    if (!json_buffer || !json_length)  return CONFIG_NULL;

    size_t pos = 0;

    // Helper lambda — appends to json_buffer, returns false if overflow
    auto append = [&](const char *s) -> bool {
        size_t len = strlen(s);
        if (pos + len + 1 > buffer_size) return false;
        memcpy(json_buffer + pos, s, len);
        pos += len;
        return true;
    };

    if (!append("{")) return CONFIG_BUFFER_TOO_SMALL;

    for (int i = 0; i < config_hndl->config_size; ++i) {
        const config_t &e = config_hndl->table[i];

        // Key
        char key_buf[64];
        snprintf(key_buf, sizeof(key_buf), "%s\"%s\":", (i > 0 ? "," : ""), e.name);
        if (!append(key_buf)) return CONFIG_BUFFER_TOO_SMALL;

        // Value
        char val_buf[256];
        switch (e.type) {
            case HSYS_TYPE_STRING: {
                const char *s = (const char *)e.p_global_value;
                // Escape backslashes and double-quotes
                char esc[256]; size_t ei = 0;
                esc[ei++] = '"';
                for (size_t si = 0; s[si] && ei < sizeof(esc) - 3; ++si) {
                    if (s[si] == '"' || s[si] == '\\') esc[ei++] = '\\';
                    esc[ei++] = s[si];
                }
                esc[ei++] = '"'; esc[ei] = '\0';
                if (!append(esc)) return CONFIG_BUFFER_TOO_SMALL;
                break;
            }
            case HSYS_TYPE_UINT32:
                snprintf(val_buf, sizeof(val_buf), "%u",
                         *((uint32_t *)e.p_global_value));
                if (!append(val_buf)) return CONFIG_BUFFER_TOO_SMALL;
                break;
            case HSYS_TYPE_BOOL:
                if (!append(*((bool *)e.p_global_value) ? "true" : "false"))
                    return CONFIG_BUFFER_TOO_SMALL;
                break;
            default:
                if (!append("null")) return CONFIG_BUFFER_TOO_SMALL;
                break;
        }
    }

    if (!append("}")) return CONFIG_BUFFER_TOO_SMALL;

    json_buffer[pos] = '\0';
    *json_length = pos;
    return CONFIG_SUCCESS;
}

// ── Minimal flat-JSON parser ──────────────────────────────────────────────────
// Parses {"key":"val","key2":123,"key3":true} into key/value string pairs.

struct KVPair { char key[32]; char value[256]; };

static int _parse_flat_json(const char *json, size_t len,
                            KVPair *pairs, int max_pairs)
{
    int count = 0;
    const char *p = json;
    const char *end = json + len;

    auto skip_ws = [&]() { while (p < end && isspace((unsigned char)*p)) ++p; };
    auto expect  = [&](char c) -> bool {
        skip_ws(); if (p < end && *p == c) { ++p; return true; } return false;
    };
    auto read_string = [&](char *out, size_t out_size) -> bool {
        if (!expect('"')) return false;
        size_t i = 0;
        while (p < end && *p != '"') {
            if (*p == '\\') { ++p; if (p >= end) return false; }
            if (i + 1 < out_size) out[i++] = *p;
            ++p;
        }
        out[i] = '\0';
        return expect('"');
    };

    skip_ws();
    if (!expect('{')) return 0;

    while (p < end) {
        skip_ws();
        if (*p == '}') break;
        if (*p == ',') { ++p; skip_ws(); }
        if (*p == '}') break;

        if (count >= max_pairs) break;

        // Key
        if (!read_string(pairs[count].key, sizeof(pairs[count].key))) break;
        if (!expect(':')) break;
        skip_ws();

        // Value — string, number, true/false/null
        char *vout = pairs[count].value;
        size_t vsize = sizeof(pairs[count].value);
        if (*p == '"') {
            if (!read_string(vout, vsize)) break;
        } else {
            // Read until , or }
            size_t vi = 0;
            while (p < end && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
                if (vi + 1 < vsize) vout[vi++] = *p;
                ++p;
            }
            vout[vi] = '\0';
        }
        ++count;
    }
    return count;
}

// ── hsys_config_load_from_json ────────────────────────────────────────────────

int32_t hsys_config_load_from_json(config_handle_t *config_hndl,
                                   const char *json_string, size_t json_length)
{
    if (!config_hndl)                  return CONFIG_NULL;
    if (!config_hndl->is_initialized)  return CONFIG_UNINTIALIZED;
    if (!json_string)                  return CONFIG_NULL;

    // Parse JSON into key-value pairs (stack-allocated, handles up to 64 fields)
    KVPair pairs[64];
    int npairs = _parse_flat_json(json_string, json_length, pairs, 64);

    if (npairs == 0) {
        LOG_MSG_ERROR(HSYS_CFG_LOG_EN, "JSON parse produced no pairs");
        return CONFIG_NULL;
    }

    for (int i = 0; i < config_hndl->config_size; ++i) {
        config_t &e = config_hndl->table[i];

        // Find matching key in parsed pairs
        const char *val = nullptr;
        for (int j = 0; j < npairs; ++j) {
            if (strcmp(pairs[j].key, e.name) == 0) { val = pairs[j].value; break; }
        }
        if (!val) continue;   // key not present in JSON — leave default

        switch (e.type) {
            case HSYS_TYPE_STRING:
                memset(e.p_global_value, 0, e.max_length);
                strncpy((char *)e.p_global_value, val, e.max_length - 1);
                break;
            case HSYS_TYPE_UINT32:
                *((uint32_t *)e.p_global_value) = (uint32_t)strtoul(val, nullptr, 10);
                break;
            case HSYS_TYPE_BOOL:
                *((bool *)e.p_global_value) = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
                break;
            default:
                break;
        }
    }

    return CONFIG_SUCCESS;
}
