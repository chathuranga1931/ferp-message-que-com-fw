// msg_http_result.cpp
//
// Fixed 16-byte pool slab layout:
//   bytes  0.. 3  uint32_t  http_result_t result
//   bytes  4.. 7  int32_t   HTTP status code
//   bytes  8..11  uint32_t  body_len
//   bytes 12..15  void *    body (heap-allocated, freed via msg->cleanup)

#include "msg_http_result.h"
#include "pal_logger.h"
#include <string.h>
#include <stdlib.h>

#define __TAG__ "MSG_HTTP"

// Cleanup hook — frees the heap body buffer stored inside the pool slab.
static void _result_cleanup(void *payload)
{
    if (!payload) return;
    void *body_ptr = nullptr;
    memcpy(&body_ptr, (uint8_t *)payload + 12U, sizeof(void *));
    free(body_ptr);
}

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

    // Heap-allocate body buffer (+ 1 for NUL terminator).
    void *body_buf = nullptr;
    if (body_len > 0U && body) {
        body_buf = malloc(body_len + 1U);
        if (body_buf) {
            memcpy(body_buf, body, body_len);
            ((char *)body_buf)[body_len] = '\0';
        } else {
            // Allocation failed — report empty body rather than crash.
            body_len = 0U;
        }
    }

    uint8_t *buf = (uint8_t *)msg->payload;
    uint32_t r   = (uint32_t)result;
    memcpy(buf,       &r,           4U);
    memcpy(buf + 4U,  &status_code, 4U);
    memcpy(buf + 8U,  &body_len,    4U);
    memcpy(buf + 12U, &body_buf,    sizeof(void *));

    // Register cleanup so body_buf is freed when the message is released.
    msg->cleanup = _result_cleanup;

    return msg;
}

MsgHttpResult::Fields MsgHttpResult::get_fields(const hsys_msg_t &msg)
{
    Fields f{};
    if (!msg.payload || msg.payload_size < 16U) return f;

    const uint8_t *buf = (const uint8_t *)msg.payload;
    uint32_t r;
    memcpy(&r,            buf,       4U);
    memcpy(&f.status_code, buf + 4U, 4U);
    memcpy(&f.body_len,    buf + 8U, 4U);
    memcpy(&f.body,        buf + 12U, sizeof(void *));

    f.result = (http_result_t)r;
    if (f.body_len == 0U) f.body = nullptr;

    return f;
}
