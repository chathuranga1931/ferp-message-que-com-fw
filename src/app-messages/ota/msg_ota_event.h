// msg_ota_event.h
//
// NOTIFICATION published by OtaModule on every session lifecycle transition.
// Subscribers: ModuleLeds, ModuleBuzzer, ModuleCloud, and any module that
// acts on OTA completion (e.g. ModuleDispTap for esp07-disptap handoff).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

typedef enum {
    OTA_EVENT_SESSION_STARTED = 0, ///< Session granted and driver handed to source
    OTA_EVENT_SESSION_ABORTED = 1, ///< Session aborted by source or OtaModule
    OTA_EVENT_COMPLETE        = 2, ///< Binary written successfully; reboot imminent (if needs_reboot)
    OTA_EVENT_TIMEOUT         = 3, ///< Session timed out (inactivity or PENDING deadline)
} ota_event_id_t;

class MsgOtaEvent : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_EVENT;

    struct Payload {
        ota_event_id_t event;       ///< Which lifecycle event occurred
        uint8_t        target_idx;  ///< Target index (0 = esp32-main, 1 = esp07-disptap)
        uint8_t        _pad[3];
        char           version[32]; ///< Version string (informational; may be empty)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaEvent(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
