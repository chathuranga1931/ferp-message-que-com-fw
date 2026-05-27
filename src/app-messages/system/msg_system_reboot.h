// msg_system_reboot.h
//
// Typed message for MSG_ID_SYSTEM_REBOOT.
//
// Published by any module (or injected from the Python test harness via
// SIM_MSG_INJECT) to request an immediate system reboot.
// No payload — presence of the message is the signal.
//
// Subscriber: ModuleSysmon — calls pal_power_reset() on receipt.
// On ESP32: triggers esp_restart().
// On macOS simulator: calls std::exit(0) (process terminates).

#ifndef MSG_SYSTEM_REBOOT_H
#define MSG_SYSTEM_REBOOT_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"   // MSG_ID_SYSTEM_REBOOT

class MsgSystemReboot : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SYSTEM_REBOOT;

    struct Payload {};

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,              // no payload bytes
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    static hsys_msg_t *create(hsys_module_id_t sender_id,
                              const Payload & = {});

    // IHsysMsg
    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}  // zero payload

    // Deserializer (no-op)
    static Payload deserialize(const hsys_msg_t & /*msg*/) { return {}; }

    /** No payload; just creates and returns the message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};

#endif // MSG_SYSTEM_REBOOT_H
