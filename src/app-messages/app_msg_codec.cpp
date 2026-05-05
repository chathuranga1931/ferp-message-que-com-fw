// app_msg_codec.cpp
//
// Generic JSON ↔ hsys_msg_t codec registry implementation.
//
// Two independent registries are provided:
//
//   1. Codec registry  — maps message name ↔ JSON serialisers (from_json /
//      to_json).  Transport-agnostic; used by PLog, sim bridge, MQTT, etc.
//
//   2. MQTT route registry — maps msg_id to MQTT routing policy (dest_module
//      and multicast_resp).  Used exclusively by ModuleMqtt for inbound
//      message dispatch.
//
// Both tables are supplied by the application and registered at startup.

#include "app_msg_codec.h"
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Codec registry storage
// ---------------------------------------------------------------------------

static const app_msg_codec_entry_t *s_table = nullptr;
static uint8_t                      s_count = 0;

void app_msg_codec_register(const app_msg_codec_entry_t *table, uint8_t count)
{
    s_table = table;
    s_count = count;
}

// ---------------------------------------------------------------------------
// MQTT route registry storage
// ---------------------------------------------------------------------------

static const app_msg_mqtt_route_t *s_route_table = nullptr;
static uint8_t                     s_route_count = 0;

void app_msg_mqtt_route_register(const app_msg_mqtt_route_t *table, uint8_t count)
{
    s_route_table = table;
    s_route_count = count;
}

// ---------------------------------------------------------------------------
// Internal lookup helpers
// ---------------------------------------------------------------------------

static const app_msg_codec_entry_t *find_by_name(const char *msg_name)
{
    if (!s_table || !msg_name) return nullptr;
    for (uint8_t i = 0; i < s_count; ++i) {
        if (strncmp(s_table[i].msg_name, msg_name, APP_MSG_CODEC_MSG_NAME_MAX) == 0) {
            return &s_table[i];
        }
    }
    return nullptr;
}

static const app_msg_codec_entry_t *find_by_id(hsys_msg_id_t id)
{
    if (!s_table) return nullptr;
    for (uint8_t i = 0; i < s_count; ++i) {
        if (s_table[i].msg_id == id) {
            return &s_table[i];
        }
    }
    return nullptr;
}

static const app_msg_mqtt_route_t *find_route_by_id(hsys_msg_id_t id)
{
    if (!s_route_table) return nullptr;
    for (uint8_t i = 0; i < s_route_count; ++i) {
        if (s_route_table[i].msg_id == id) {
            return &s_route_table[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Codec public API
// ---------------------------------------------------------------------------

hsys_msg_t *app_msg_codec_decode(const char *msg_name, const char *data_json,
                                  hsys_module_id_t sender_id)
{
    const app_msg_codec_entry_t *e = find_by_name(msg_name);
    if (!e || !e->from_json) return nullptr;
    return e->from_json(data_json, sender_id);
}

int32_t app_msg_codec_encode(const hsys_msg_t *msg,
                              char             *msg_name_out,
                              uint32_t          name_len,
                              char             *data_json_out,
                              uint32_t          data_len)
{
    if (!msg || !msg_name_out || !data_json_out) return -1;

    const app_msg_codec_entry_t *e = find_by_id(msg->msg_id);
    if (!e || !e->to_json) return -1;

    strncpy(msg_name_out, e->msg_name, name_len - 1);
    msg_name_out[name_len - 1] = '\0';

    return e->to_json(msg, data_json_out, data_len);
}

// ---------------------------------------------------------------------------
// MQTT route public API
// ---------------------------------------------------------------------------

hsys_module_id_t app_msg_mqtt_route_get_dest(hsys_msg_id_t msg_id)
{
    const app_msg_mqtt_route_t *r = find_route_by_id(msg_id);
    if (!r) return (hsys_module_id_t)0;
    return r->dest_module;
}

bool app_msg_mqtt_route_is_multicast(hsys_msg_id_t msg_id)
{
    const app_msg_mqtt_route_t *r = find_route_by_id(msg_id);
    if (!r) return false;
    return r->multicast_resp;
}
