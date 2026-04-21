// msg_sd_ready.h
//
// Published by ModuleSD once the SD card is mounted and ready.
// No payload — presence of the message is the signal.

#ifndef MSG_SD_READY_H
#define MSG_SD_READY_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgSdReady : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SD_READY;

    struct Payload {};

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    static hsys_msg_t *create(hsys_module_id_t sender_id,
                              const Payload & = {});

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}

    static Payload deserialize(const hsys_msg_t & /*msg*/) { return {}; }

#ifdef FERP_SIMULATOR
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif
};

#endif // MSG_SD_READY_H
