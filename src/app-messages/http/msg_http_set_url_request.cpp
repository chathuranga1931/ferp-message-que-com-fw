// msg_http_set_url_request.cpp

#include "msg_http_set_url_request.h"
#include "pal_logger.h"
#include <string.h>
#include <stddef.h>

#define __TAG__ "MSG_HTTP"

hsys_msg_t *MsgHttpSetUrlRequest::create(hsys_module_id_t sender_id, const char *url)
{
    if (!url) return nullptr;

    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(true, "SetUrlRequest: pool full");
        return nullptr;
    }

    // Store char_count (without NUL) then the URL string (NUL-terminated)
    uint32_t char_count = (uint32_t)strnlen(url, MODULE_HTTP_MAX_URL_LEN - 1U);
    memcpy(msg->payload, &char_count, 4U);
    memcpy((uint8_t *)msg->payload + 4U, url, char_count);
    ((uint8_t *)msg->payload)[4U + char_count] = '\0';

    return msg;
}

const char *MsgHttpSetUrlRequest::get_url(const hsys_msg_t &msg)
{
    if (!msg.payload || msg.payload_size < 5U) return nullptr;
    return (const char *)msg.payload + 4U;
}
