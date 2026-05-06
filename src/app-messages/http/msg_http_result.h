// msg_http_result.h
//
// Sent DIRECT by ModuleHttp to the session owner after the HTTP call
// completes (or if the session expires / errors).
//
// Payload layout (fixed 16-byte pool slab):
//   bytes  0.. 3  uint32_t   http_result_t result
//   bytes  4.. 7  int32_t    HTTP status code (200, 404 …; 0 if not received)
//   bytes  8..11  uint32_t   body_len  (0 when no body)
//   bytes 12..15  void *     body  — heap-allocated; freed automatically via
//                                   msg->cleanup when the message is released.
//
// The body pointer is valid until the last hsys_msg_release() on this message.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include <stdint.h>

class MsgHttpResult : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_RESULT;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      (uint16_t)MODULE_HTTP_PAYLOAD_RESULT,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    /// Deserialized view of the result payload.
    struct Fields {
        http_result_t result;
        int32_t       status_code;
        uint32_t      body_len;
        const void   *body;   ///< Heap-allocated body buffer; valid until the last msg release
    };

    MsgHttpResult() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}  // not used — create() writes directly

    /// Create a result message.
    /// @p body     Optional response body pointer (may be nullptr when body_len == 0).
    /// @p body_len Number of body bytes; capped at MODULE_HTTP_MAX_RESPONSE_BODY.
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               http_result_t result,
                               int32_t       status_code,
                               const void   *body,
                               uint32_t      body_len);

    /// Read result fields from the payload.  body pointer is valid until the
    /// message is released by the framework.
    static Fields get_fields(const hsys_msg_t &msg);
};
