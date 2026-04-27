// msg_ota_progress.cpp

#include "msg_ota_progress.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaProgress::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaProgress::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaProgress instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaProgress::Payload MsgOtaProgress::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
