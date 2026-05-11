// msg_system_status.h
//
// Published by ModuleMsgTranslator when the aggregated system busy/idle state changes.
//
// The status is derived by translating incoming OTA and fuel events:
//   - OTA session started  → SYSTEM_STATUS_BUSY
//   - OTA session ended    → (re-evaluate; may become IDLE)
//   - Nozzle PUMPING       → SYSTEM_STATUS_BUSY
//   - Nozzle IDLE/PUMPED   → (re-evaluate; may become IDLE)
//
// The message is published only when the status *changes* — subscribers
// will not receive duplicate events for the same state.
//
// Subscriber example:
//
//   case MsgSystemStatus::ID: {
//       auto p = MsgSystemStatus::deserialize(msg);
//       if (p.status == SYSTEM_STATUS_IDLE) { ... }
//       break;
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Status enum
// ---------------------------------------------------------------------------

typedef enum : uint8_t {
    SYSTEM_STATUS_IDLE     = 0,   ///< No OTA or fueling in progress
    SYSTEM_STATUS_BUSY     = 1,   ///< OTA or fueling is active
    SYSTEM_STATUS_MODERATE = 2,   ///< Reserved — not yet defined
} system_status_t;

// ---------------------------------------------------------------------------
// MsgSystemStatus
// ---------------------------------------------------------------------------

class MsgSystemStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SYSTEM_STATUS;

    struct Payload {
        system_status_t status;   ///< Current aggregated system status
        uint8_t         _pad[3];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgSystemStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
