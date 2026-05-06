// msg_http_set_root_ca_request.h
//
// Sent DIRECT by the session owner to ModuleHttp to supply a custom CA
// certificate PEM for TLS verification.  The PEM string must point to
// static/flash memory that remains valid for the duration of the session.
// Pass cert_pem = nullptr to use the platform-bundled CA bundle.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include <stdint.h>

class MsgHttpSetRootCaRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_SET_ROOT_CA_REQ;

    struct Payload {
        const char *cert_pem;  ///< Pointer to PEM string; NULL = use cert bundle
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgHttpSetRootCaRequest(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
