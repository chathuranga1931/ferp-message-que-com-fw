// msg_ota_start_request.h
//
// Sent DIRECT by an OTA source to OtaModule to request a new OTA session.
// OtaModule replies with MsgOtaStartResponse.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgOtaStartRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_START_REQUEST;

    struct Payload {
        uint8_t target_idx;           ///< Target index (0 = esp32-main, 1 = esp07-disptap)
        uint8_t _pad[3];
        char    incoming_version[32]; ///< Version string of firmware to install (informational)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaStartRequest(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
