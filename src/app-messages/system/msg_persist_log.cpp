// msg_persist_log.cpp
//
// MsgPersistLog implementation.

#include "msg_persist_log.h"
#include "hsys_msg.h"
#include <string.h>

void MsgPersistLog::serialize(hsys_msg_t *msg) const
{
    if (msg && msg->payload) {
        memcpy(msg->payload, &_p, sizeof(_p));
    }
}

hsys_msg_t *MsgPersistLog::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) return nullptr;
    MsgPersistLog obj(p);
    obj.serialize(msg);
    return msg;
}

MsgPersistLog::Payload MsgPersistLog::deserialize(const hsys_msg_t &msg)
{
    Payload p = {};
    if (msg.payload && msg.payload_size >= sizeof(p)) {
        memcpy(&p, msg.payload, sizeof(p));
    }
    return p;
}
