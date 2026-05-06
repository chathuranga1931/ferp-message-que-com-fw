// msg_http_response_header.cpp
//
// Payload layout identical to MsgHttpHeaderRequest:
//   [uint32 key_len][key bytes][NUL][uint32 val_len][value bytes][NUL]

#include "msg_http_response_header.h"
#include "pal_logger.h"
#include <string.h>
#include <stddef.h>

#define __TAG__ "MSG_HTTP"

hsys_msg_t *MsgHttpResponseHeader::create(hsys_module_id_t sender_id,
                                           const char *key, const char *value)
{
    if (!key || !value) return nullptr;

    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(true, "ResponseHeader: pool full");
        return nullptr;
    }

    uint8_t  *buf     = (uint8_t *)msg->payload;
    uint32_t  key_len = (uint32_t)strnlen(key,   MODULE_HTTP_MAX_HEADER_KEY - 1U);
    uint32_t  val_len = (uint32_t)strnlen(value, MODULE_HTTP_MAX_HEADER_VAL - 1U);

    size_t off = 0U;
    memcpy(buf + off, &key_len, 4U);   off += 4U;
    memcpy(buf + off, key, key_len);   off += key_len;
    buf[off++] = '\0';

    memcpy(buf + off, &val_len, 4U);   off += 4U;
    memcpy(buf + off, value, val_len); off += val_len;
    buf[off]   = '\0';

    return msg;
}

const char *MsgHttpResponseHeader::get_key(const hsys_msg_t &msg)
{
    if (!msg.payload || msg.payload_size < 5U) return nullptr;
    return (const char *)msg.payload + 4U;
}

const char *MsgHttpResponseHeader::get_value(const hsys_msg_t &msg)
{
    if (!msg.payload || msg.payload_size < 9U) return nullptr;
    const uint8_t *buf = (const uint8_t *)msg.payload;
    uint32_t key_len;
    memcpy(&key_len, buf, 4U);
    if (key_len >= MODULE_HTTP_MAX_HEADER_KEY) return nullptr;
    size_t val_off = 4U + key_len + 1U + 4U;
    if (val_off >= msg.payload_size) return nullptr;
    return (const char *)buf + val_off;
}
