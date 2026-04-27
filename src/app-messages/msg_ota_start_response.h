// msg_ota_start_response.h
//
// Sent DIRECT by OtaModule to the requesting source in reply to MsgOtaStartRequest.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

typedef enum {
    OTA_START_ACCEPTED               = 0, ///< Session granted; send MsgOtaRequestDriver next
    OTA_START_REJECTED_BUSY          = 1, ///< Another session is already active
    OTA_START_REJECTED_UNKNOWN_SOURCE= 2, ///< sender_id not in source table
    OTA_START_REJECTED_UNKNOWN_TARGET= 3, ///< target_idx not in target table
} ota_start_result_t;

class MsgOtaStartResponse : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_START_RESPONSE;

    struct Payload {
        ota_start_result_t result;
        uint8_t            _pad[4];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaStartResponse(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
