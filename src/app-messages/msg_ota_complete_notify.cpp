// msg_ota_complete_notify.cpp

#include "msg_ota_complete_notify.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaCompleteNotify::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaCompleteNotify::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaCompleteNotify instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaCompleteNotify::Payload MsgOtaCompleteNotify::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
