// msg_ota_request_driver.h
//
// Sent DIRECT by the OTA source to OtaModule after receiving ACCEPTED.
// No payload — sender_id (from the HSYS envelope) identifies the session.
// OtaModule replies with MsgOtaDriverResponse.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgOtaRequestDriver : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_REQUEST_DRIVER;

    struct Payload {
        uint8_t _reserved; ///< No payload — sender_id in envelope identifies the session
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaRequestDriver() : _p{} {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id);

private:
    Payload _p;
};
