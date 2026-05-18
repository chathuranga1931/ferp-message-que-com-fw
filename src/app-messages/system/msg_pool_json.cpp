// msg_pool_json.cpp
//
// Pool-slab layout (sizeof(Payload) bytes):
//   [0..3]  uint32_t  json_len   — length of JSON string (excl. null)
//   [4..7]  pad
//   [8..]   void *    json_buf   — pointer to separately pool-alloc'd char[]
//
// The inner json_buf is allocated via hsys_pool_alloc(json_len + 1) so that
// the pool picks the smallest fitting size class automatically.
// msg->cleanup frees json_buf back to the pool when ref_count hits zero.

#include "msg_pool_json.h"
#include "hsys_pool.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__    "POOL_JSN"
#ifndef MSG_PJ_LOG_EN
#define MSG_PJ_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Cleanup hook — returns the inner JSON buffer to the pool.
// Called once by the framework when the message's ref_count reaches zero.
// ---------------------------------------------------------------------------

static void _pool_json_cleanup(void *payload)
{
    if (!payload) return;
    void *json_buf = nullptr;
    memcpy(&json_buf, (uint8_t *)payload + 8U, sizeof(void *));
    hsys_pool_free(json_buf);
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

hsys_msg_t *MsgPoolJson::create(hsys_module_id_t sender_id,
                                 const char *json, uint32_t json_len)
{
    if (!json || json_len == 0) return nullptr;

    // Allocate pool buffer for the JSON string (+1 for null terminator).
    // hsys_pool_alloc selects the smallest fitting size class automatically.
    char *buf = static_cast<char *>(
        hsys_pool_alloc(static_cast<uint16_t>(json_len + 1U)));
    if (!buf) {
        LOG_MSG_ERROR(MSG_PJ_LOG_EN,
                      "create: pool alloc for JSON failed (len=%lu)",
                      (unsigned long)json_len);
        return nullptr;
    }
    memcpy(buf, json, json_len);
    buf[json_len] = '\0';

    // Allocate the message slab.
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_PJ_LOG_EN, "create: hsys_msg_create failed");
        hsys_pool_free(buf);
        return nullptr;
    }

    uint8_t *slab = static_cast<uint8_t *>(msg->payload);
    memset(slab, 0, sizeof(Payload));
    memcpy(slab,      &json_len, sizeof(uint32_t));
    memcpy(slab + 8U, &buf,      sizeof(void *));

    // Register cleanup so buf is returned to the pool on message release.
    msg->cleanup = _pool_json_cleanup;
    return msg;
}

// ---------------------------------------------------------------------------
// get_json()
// ---------------------------------------------------------------------------

const char *MsgPoolJson::get_json(const hsys_msg_t &msg, uint32_t *len_out)
{
    if (!msg.payload || msg.payload_size < sizeof(Payload)) {
        if (len_out) *len_out = 0U;
        return nullptr;
    }
    const uint8_t *slab = static_cast<const uint8_t *>(msg.payload);
    uint32_t json_len = 0U;
    void    *json_buf = nullptr;
    memcpy(&json_len, slab,      sizeof(uint32_t));
    memcpy(&json_buf, slab + 8U, sizeof(void *));
    if (len_out) *len_out = json_len;
    return static_cast<const char *>(json_buf);
}

// ---------------------------------------------------------------------------
// Codec: from_json — inbound MsgPoolJson from the wire is not meaningful.
// ---------------------------------------------------------------------------

hsys_msg_t *MsgPoolJson::from_json(const char * /*data_json*/,
                                    hsys_module_id_t /*sender_id*/)
{
    return nullptr;   // response-only; never decoded from the wire
}

// ---------------------------------------------------------------------------
// Codec: to_json — emit the JSON string directly as the "data" field.
// ---------------------------------------------------------------------------

int32_t MsgPoolJson::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    if (!msg) return -1;
    uint32_t json_len = 0U;
    const char *json = get_json(*msg, &json_len);
    if (!json || json_len == 0U) {
        if (buf_len >= 3U) { data_json[0]='{'; data_json[1]='}'; data_json[2]='\0'; }
        return 0;
    }
    if (json_len + 1U > buf_len) {
        LOG_MSG_ERROR(MSG_PJ_LOG_EN,
                      "to_json: JSON too large for codec buffer (%lu > %lu)",
                      (unsigned long)json_len, (unsigned long)buf_len - 1U);
        return -1;
    }
    memcpy(data_json, json, json_len + 1U);
    return 0;
}
