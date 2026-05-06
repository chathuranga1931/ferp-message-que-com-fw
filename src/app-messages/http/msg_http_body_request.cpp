// msg_http_body_request.cpp
//
// Payload layout: [uint32 len][len bytes of body data]

#include "msg_http_body_request.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

hsys_msg_t *MsgHttpBodyRequest::create(hsys_module_id_t sender_id,
                                        const void *body, uint32_t len)
{
    if (!body && len > 0U) return nullptr;

    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(true, "BodyRequest: pool full");
        return nullptr;
    }

    if (len > MODULE_HTTP_MAX_REQUEST_BODY) {
        len = MODULE_HTTP_MAX_REQUEST_BODY;
    }

    memcpy(msg->payload, &len, 4U);
    if (len > 0U && body) {
        memcpy((uint8_t *)msg->payload + 4U, body, len);
    }
    return msg;
}

const void *MsgHttpBodyRequest::get_body(const hsys_msg_t &msg, uint32_t *len_out)
{
    if (!msg.payload || msg.payload_size < 4U) {
        if (len_out) *len_out = 0U;
        return nullptr;
    }
    uint32_t len;
    memcpy(&len, msg.payload, 4U);
    if (len_out) *len_out = len;
    if (len == 0U) return nullptr;
    return (const uint8_t *)msg.payload + 4U;
}
