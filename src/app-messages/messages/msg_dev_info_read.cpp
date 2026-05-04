// msg_dev_info_read.cpp

#include "msg_dev_info_read.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_DIIR"
#ifndef MSG_DIIR_LOG_EN
#define MSG_DIIR_LOG_EN true
#endif

void MsgDevInfoRead::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgDevInfoRead::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) { LOG_MSG_ERROR(MSG_DIIR_LOG_EN, "create failed"); return nullptr; }
    MsgDevInfoRead instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgDevInfoRead::Payload MsgDevInfoRead::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload) memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
