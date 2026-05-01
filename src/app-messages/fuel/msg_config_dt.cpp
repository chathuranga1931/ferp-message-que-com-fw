// msg_config_dt.cpp

#include "msg_config_dt.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_CDT "
#ifndef MSG_CDT_LOG_EN
#define MSG_CDT_LOG_EN true
#endif

void MsgConfigDT::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigDT::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CDT_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigDT instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigDT::Payload MsgConfigDT::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}
