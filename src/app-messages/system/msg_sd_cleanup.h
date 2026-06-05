// msg_sd_cleanup.h
//
// Typed message for MSG_ID_SD_CLEANUP.
//
// Published by any module (or injected from the device tool via MQTT) to
// request a full SD-card wipe: every file and sub-directory is deleted,
// then the device reboots automatically.
// No payload — presence of the message is the signal.
//
// Subscriber: ModuleSD — calls app_sd_cleanup() then publishes MsgSystemReboot.

#ifndef MSG_SD_CLEANUP_H
#define MSG_SD_CLEANUP_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"   // MSG_ID_SD_CLEANUP

class MsgSdCleanup : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SD_CLEANUP;

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

    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};

#endif // MSG_SD_CLEANUP_H
