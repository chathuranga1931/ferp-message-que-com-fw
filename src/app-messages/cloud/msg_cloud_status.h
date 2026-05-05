// msg_cloud_status.h
//
// Published by ModuleCloud to inform other modules of cloud operation results.
//
// Subscriber:
//
//   case MsgCloudStatus::ID: {
//       auto p = MsgCloudStatus::deserialize(msg);
//       if (p.event == CLOUD_STATUS_REGISTERED) { ... }
//       break;
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Event enum
// ---------------------------------------------------------------------------

typedef enum : uint8_t {
    CLOUD_STATUS_REGISTERED      = 0,   ///< Device provisioned successfully
    CLOUD_STATUS_REGISTER_FAILED = 1,   ///< Provisioning failed — will retry
    CLOUD_STATUS_PUMPED_SUCCESS  = 2,   ///< Fuel-pumped event sent to cloud
    CLOUD_STATUS_PUMPED_FAILED   = 3,   ///< Fuel-pumped event failed — handed to retransmit
    CLOUD_STATUS_HB_SENT         = 4,   ///< Heartbeat sent
    CLOUD_STATUS_HB_FAILED       = 5,   ///< Heartbeat failed
} cloud_status_event_t;

// ---------------------------------------------------------------------------
// MsgCloudStatus
// ---------------------------------------------------------------------------

class MsgCloudStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CLOUD_STATUS;

    struct Payload {
        cloud_status_event_t event;
        uint8_t              nozzle_idx;    ///< valid for PUMPED_* events
        uint8_t              _pad[2];
        char                 device_uuid[50]; ///< device UUID — valid only for CLOUD_STATUS_REGISTERED (SIZE_OF_UUID=50)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgCloudStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

    /** Parse a flat JSON payload and return a ready-to-publish message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
