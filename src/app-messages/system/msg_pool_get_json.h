// msg_pool_get_json.h
//
// MsgPoolGetJson — zero-payload command.
//
// Any module (or external MQTT / WebAPI client) sends this to request a
// message-pool status snapshot.  ModuleSysmon handles it and responds by
// publishing MsgPoolJson (broadcast).
//
// Usage (publisher):
//   hsys_msg_t *msg = MsgPoolGetJson::create(id());
//   publish(msg);   // or send(msg, MODULE_SYSMON_ID) for direct
//
// Wire data field: {}

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgPoolGetJson : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_POOL_GET_JSON;

    struct Payload {};

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,              ///< no payload bytes
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}

    static hsys_msg_t *create(hsys_module_id_t sender_id);
    static Payload     deserialize(const hsys_msg_t & /*msg*/) { return {}; }

    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};
