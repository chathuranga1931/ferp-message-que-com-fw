// msg_http_response_header.h
//
// Sent DIRECT by ModuleHttp to the session owner for each response header
// received.  All MsgHttpResponseHeader messages are sent before MsgHttpResult.
// Payload layout is identical to MsgHttpHeaderRequest:
//   [uint32 key_len][key bytes (NUL-terminated)][uint32 val_len][value bytes (NUL-terminated)]
// key_len and val_len are the character counts excluding NUL.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include <stdint.h>

class MsgHttpResponseHeader : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_RESPONSE_HEADER;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      (uint16_t)MODULE_HTTP_PAYLOAD_HDR,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgHttpResponseHeader() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}  // not used — create() writes directly

    /// Create a response header message.
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               const char *key, const char *value);

    /// Return pointer to the null-terminated header key stored in the payload.
    static const char *get_key(const hsys_msg_t &msg);

    /// Return pointer to the null-terminated header value stored in the payload.
    static const char *get_value(const hsys_msg_t &msg);
};
