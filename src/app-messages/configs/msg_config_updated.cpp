// msg_config_updated.cpp
//
// MsgConfigUpdated — factory implementation.

#include "msg_config_updated.h"
#include "pal_logger.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define __TAG__ "MSG_CFGU"
#ifndef MSG_CFGU_LOG_EN
#define MSG_CFGU_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigUpdated::create(hsys_module_id_t sender_id, uint16_t key)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_CFGU_LOG_EN, "create: pool full");
        return nullptr;
    }
    uint8_t *p = static_cast<uint8_t *>(msg->payload);
    memset(p, 0, SLAB_SIZE);
    p[OFF_KEY + 0] = (uint8_t)(key & 0xFFu);
    p[OFF_KEY + 1] = (uint8_t)(key >> 8u);
    return msg;
}

// ---------------------------------------------------------------------------
// Accessor
// ---------------------------------------------------------------------------

uint16_t MsgConfigUpdated::get_key(const hsys_msg_t &msg)
{
    if (!msg.payload) return 0;
    const uint8_t *p = static_cast<const uint8_t *>(msg.payload);
    return (uint16_t)(p[OFF_KEY] | ((uint16_t)p[OFF_KEY + 1] << 8u));
}

// ---------------------------------------------------------------------------
// JSON bridge
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigUpdated::from_json(const char *data_json, hsys_module_id_t sender_id)
{
    uint16_t key = 0;
    if (data_json) {
        unsigned int k = 0;
        if (sscanf(data_json, "{\"key\":%u}", &k) == 1) {
            key = (uint16_t)k;
        }
    }
    return create(sender_id, key);
}

int32_t MsgConfigUpdated::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    if (!msg || !data_json_out || buf_len == 0) return -1;
    uint16_t key = get_key(*msg);
    int n = snprintf(data_json_out, buf_len, "{\"key\":%u}", (unsigned)key);
    return (n > 0) ? 0 : -1;
}
