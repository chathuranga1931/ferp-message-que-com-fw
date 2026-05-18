// msg_file_list_spiffs.cpp
//
// Slab layout (sizeof(Payload) bytes, fits 32-byte pool class on ESP32):
//   [0..3]  uint32_t  json_len   — byte length of JSON string (excl. NUL)
//   [4..7]  pad
//   [8..]   void *    json_buf   — pointer to separately pool-alloc'd char[]
//
// msg->cleanup frees json_buf when ref_count reaches zero.

#define __TAG__  "SPIF_FLS"  // exactly 8 chars

#include "msg_file_list_spiffs.h"
#include "hsys_pool.h"
#include "pal_logger.h"
#include <string.h>

#ifndef MSG_FLS_LOG_EN
#define MSG_FLS_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Cleanup hook — called by the framework when ref_count hits zero.
// ---------------------------------------------------------------------------

static void _file_list_spiffs_cleanup(void *payload)
{
    if (!payload) return;
    void *json_buf = nullptr;
    memcpy(&json_buf, (uint8_t *)payload + 8U, sizeof(void *));
    if (json_buf) hsys_pool_free(json_buf);
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

hsys_msg_t *MsgFileListSpiffs::create(hsys_module_id_t sender_id,
                                       const char *json, uint32_t json_len)
{
    if (!json || json_len == 0) return nullptr;

    // Allocate inner buffer for the JSON string (+ NUL terminator).
    char *buf = static_cast<char *>(
        hsys_pool_alloc(static_cast<uint16_t>(json_len + 1U)));
    if (!buf) {
        LOG_MSG_ERROR(MSG_FLS_LOG_EN,
                      "create: pool alloc for JSON failed (len=%lu)",
                      (unsigned long)json_len);
        return nullptr;
    }
    memcpy(buf, json, json_len);
    buf[json_len] = '\0';

    // Allocate the fixed-size message slab.
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_FLS_LOG_EN, "create: hsys_msg_create failed");
        hsys_pool_free(buf);
        return nullptr;
    }

    uint8_t *slab = static_cast<uint8_t *>(msg->payload);
    memset(slab, 0, sizeof(Payload));
    memcpy(slab,      &json_len, sizeof(uint32_t));
    memcpy(slab + 8U, &buf,      sizeof(void *));

    msg->cleanup = _file_list_spiffs_cleanup;
    return msg;
}

// ---------------------------------------------------------------------------
// get_json()
// ---------------------------------------------------------------------------

const char *MsgFileListSpiffs::get_json(const hsys_msg_t &msg, uint32_t *len_out)
{
    if (!msg.payload) return nullptr;
    const uint8_t *slab = static_cast<const uint8_t *>(msg.payload);

    uint32_t json_len = 0;
    memcpy(&json_len, slab, sizeof(uint32_t));

    void *json_buf = nullptr;
    memcpy(&json_buf, slab + 8U, sizeof(void *));

    if (len_out) *len_out = json_len;
    return static_cast<const char *>(json_buf);
}

// ---------------------------------------------------------------------------
// to_json() — codec serialisation: copy the inner JSON string directly.
// ---------------------------------------------------------------------------

int32_t MsgFileListSpiffs::to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len)
{
    if (!msg || !buf || buf_len == 0) return 0;

    uint32_t json_len = 0;
    const char *json = get_json(*msg, &json_len);
    if (!json || json_len == 0) {
        if (buf_len >= 3) { buf[0]='{'; buf[1]='}'; buf[2]='\0'; }
        return 2;
    }

    uint32_t copy = (json_len < (buf_len - 1U)) ? json_len : (buf_len - 1U);
    memcpy(buf, json, copy);
    buf[copy] = '\0';
    return (int32_t)copy;
}

// ---------------------------------------------------------------------------
// from_json() — response-only; inbound decode not supported.
// ---------------------------------------------------------------------------

hsys_msg_t *MsgFileListSpiffs::from_json(const char * /*data_json*/,
                                          hsys_module_id_t /*sender_id*/)
{
    return nullptr;
}
