// msg_http_set_url_request.h
//
// Sent DIRECT by the session owner to ModuleHttp to set the request URL.
// The payload uses the 4-byte length-prefix protocol:
//   [uint32 char_count][char_count bytes of URL][NUL]
// Maximum URL length is MODULE_HTTP_MAX_URL_LEN - 1 characters.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include <stdint.h>

class MsgHttpSetUrlRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_SET_URL_REQUEST;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      (uint16_t)MODULE_HTTP_PAYLOAD_URL,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // No Payload struct — payload is raw variable-length bytes.
    MsgHttpSetUrlRequest() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}  // not used — create() writes directly

    /// Create and fill a SetUrlRequest message.
    /// @p url  Null-terminated URL string (at most MODULE_HTTP_MAX_URL_LEN-1 chars).
    static hsys_msg_t *create(hsys_module_id_t sender_id, const char *url);

    /// Return a pointer to the null-terminated URL stored in the payload.
    /// Returns nullptr if the message has no payload.
    static const char *get_url(const hsys_msg_t &msg);
};
