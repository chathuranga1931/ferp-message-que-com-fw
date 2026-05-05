// app_msg_codec.cpp
//
// Generic JSON ↔ hsys_msg_t codec registry implementation.
//
// This file contains only the registry infrastructure: storage, lookup, and
// the public dispatch API. It has no knowledge of specific message types.
//
// The application codec table (which messages are supported and how to
// encode/decode them) is defined in product/app/app.cpp and registered at
// startup via app_msg_codec_register().

#include "app_msg_codec.h"
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Registry storage
// ---------------------------------------------------------------------------

static const app_msg_codec_entry_t *s_table = nullptr;
static uint8_t                      s_count = 0;

void app_msg_codec_register(const app_msg_codec_entry_t *table, uint8_t count)
{
    s_table = table;
    s_count = count;
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

hsys_msg_t *app_msg_codec_decode(const char *msg_name, const char *data_json,
                                  hsys_module_id_t sender_id)
{
    const app_msg_codec_entry_t *e = find_by_name(msg_name);
    if (!e || !e->from_json) return nullptr;
    return e->from_json(data_json, sender_id);
}

hsys_module_id_t app_msg_codec_get_dest(const char *msg_name)
{
    const app_msg_codec_entry_t *e = find_by_name(msg_name);
    if (!e) return (hsys_module_id_t)0;
    return e->dest_module;
}

bool app_msg_codec_is_multicast(const char *msg_name)
{
    const app_msg_codec_entry_t *e = find_by_name(msg_name);
    if (!e) return false;
    return e->multicast_resp;
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
