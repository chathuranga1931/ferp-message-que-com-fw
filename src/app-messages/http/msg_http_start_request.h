// msg_http_start_request.h
//
// Sent DIRECT by a client module to ModuleHttp to open an HTTP session.
// ModuleHttp replies with MsgHttpStartResponse.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include "pal_http_client.h"
#include <stdint.h>

/// Maximum number of response header keys a client may ask ModuleHttp to
/// capture and forward back as MsgHttpResponseHeader messages.
#define MSG_HTTP_MAX_COLLECT_KEYS  4U

class MsgHttpStartRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_START_REQUEST;

    struct Payload {
        pal_http_method_t method;      ///< HTTP method for this session
        uint32_t          timeout_ms;  ///< Request timeout; 0 = use PAL default
        uint8_t           collect_count; ///< Number of response header keys to capture (0 = none)
        uint8_t           _pad[3];
        /// Response header key names to capture (NUL-terminated, unused slots are empty).
        char collect_keys[MSG_HTTP_MAX_COLLECT_KEYS][MODULE_HTTP_MAX_HEADER_KEY];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgHttpStartRequest(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
