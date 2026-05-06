// msg_http_body_request.cpp
//
// Fixed 16-byte pool slab layout:
//   bytes  0.. 3  uint32_t  body_len
//   bytes  4.. 7  (padding)
//   bytes  8..15  void *    body (heap-allocated, freed via msg->cleanup)
//
// The pool class for a 16-byte descriptor is {32, ...}, so the 32-byte slab
// safely holds the pointer on both 32-bit ESP32 and 64-bit Mac simulator.

#include "msg_http_body_request.h"
#include "pal_logger.h"
#include <string.h>
#include <stdlib.h>

#define __TAG__ "MSG_HTTP"

// Cleanup hook — frees the heap body buffer stored inside the pool slab.
static void _body_cleanup(void *payload)
{
    if (!payload) return;
    void *body_ptr = nullptr;
    memcpy(&body_ptr, (uint8_t *)payload + 8U, sizeof(void *));
    free(body_ptr);
}

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

    // Heap-allocate body buffer (+ 1 for NUL terminator on text bodies).
    void *body_buf = nullptr;
    if (len > 0U && body) {
        body_buf = malloc(len + 1U);
        if (body_buf) {
            memcpy(body_buf, body, len);
            ((char *)body_buf)[len] = '\0';
        } else {
            len = 0U;
        }
    }

    uint8_t *buf = (uint8_t *)msg->payload;
    memset(buf, 0, 16U);
    memcpy(buf,      &len,      4U);
    memcpy(buf + 8U, &body_buf, sizeof(void *));

    msg->cleanup = _body_cleanup;
    return msg;
}

const void *MsgHttpBodyRequest::get_body(const hsys_msg_t &msg, uint32_t *len_out)
{
    if (!msg.payload || msg.payload_size < 16U) {
        if (len_out) *len_out = 0U;
        return nullptr;
    }
    const uint8_t *buf = (const uint8_t *)msg.payload;
    uint32_t len;
    memcpy(&len, buf, 4U);
    if (len_out) *len_out = len;
    if (len == 0U) return nullptr;
    void *body_ptr = nullptr;
    memcpy(&body_ptr, buf + 8U, sizeof(void *));
    return body_ptr;
}

