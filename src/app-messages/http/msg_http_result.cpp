// msg_http_result.cpp
//
// Payload layout:
//   bytes  0.. 3  uint32_t  http_result_t result
//   bytes  4.. 7  int32_t   HTTP status code
//   bytes  8..11  uint32_t  body_len
//   bytes 12..N   body data (body_len bytes, NUL-terminated)

#include "msg_http_result.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

hsys_msg_t *MsgHttpResult::create(hsys_module_id_t sender_id,
                                   http_result_t result,
                                   int32_t       status_code,
                                   const void   *body,
                                   uint32_t      body_len)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(true, "Result: pool full");
        return nullptr;
    }

    if (body_len > MODULE_HTTP_MAX_RESPONSE_BODY) {
        body_len = MODULE_HTTP_MAX_RESPONSE_BODY;
    }

    uint8_t *buf = (uint8_t *)msg->payload;
    uint32_t r   = (uint32_t)result;

    memcpy(buf,      &r,           4U);
    memcpy(buf + 4U, &status_code, 4U);
    memcpy(buf + 8U, &body_len,    4U);

    if (body_len > 0U && body) {
        memcpy(buf + 12U, body, body_len);
    }
    buf[12U + body_len] = '\0';  // NUL-terminate the body (always safe — payload covers it)

    return msg;
}

MsgHttpResult::Fields MsgHttpResult::get_fields(const hsys_msg_t &msg)
{
    Fields f{};
    if (!msg.payload || msg.payload_size < 12U) return f;

    const uint8_t *buf = (const uint8_t *)msg.payload;
    uint32_t r;
    memcpy(&r,          buf,      4U);
    memcpy(&f.status_code, buf + 4U, 4U);
    memcpy(&f.body_len,    buf + 8U, 4U);

    f.result = (http_result_t)r;
    f.body   = (f.body_len > 0U) ? (const void *)(buf + 12U) : nullptr;

    return f;
}
