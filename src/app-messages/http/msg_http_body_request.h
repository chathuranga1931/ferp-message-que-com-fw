// msg_http_body_request.h
//
// Sent DIRECT by the session owner to ModuleHttp to set the request body
// (for POST / PUT).  Optional for GET / DELETE.
// Fixed 16-byte pool slab; actual body is heap-allocated and freed via cleanup.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include <stdint.h>

class MsgHttpBodyRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_BODY_REQUEST;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      (uint16_t)MODULE_HTTP_PAYLOAD_BODY,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgHttpBodyRequest() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}  // not used — create() writes directly

    /// Create a body message.
    /// @p body  Pointer to body data (may be binary).
    /// @p len   Number of bytes.  Capped at MODULE_HTTP_MAX_REQUEST_BODY.
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               const void *body, uint32_t len);

    /// Return pointer to the body data and set *len_out to the byte count.
    /// Returns nullptr if no payload is available.
    static const void *get_body(const hsys_msg_t &msg, uint32_t *len_out);
};
