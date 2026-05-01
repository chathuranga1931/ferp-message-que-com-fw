// msg_ota_abort_request.h
//
// Sent DIRECT by the OTA source to OtaModule to cancel the active session.
// OtaModule will call ferase(ctx), publish MsgOtaEvent(SESSION_ABORTED), then return to IDLE.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

typedef enum {
    OTA_ABORT_SOURCE_CANCELLED    = 0, ///< Source voluntarily cancelled
    OTA_ABORT_WRITE_ERROR         = 1, ///< Driver write failure reported by source
    OTA_ABORT_SOURCE_DISCONNECTED = 2, ///< Underlying transport lost
} ota_abort_reason_t;

class MsgOtaAbortRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_ABORT_REQUEST;

    struct Payload {
        ota_abort_reason_t reason;
        uint8_t            _pad[4];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaAbortRequest(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
