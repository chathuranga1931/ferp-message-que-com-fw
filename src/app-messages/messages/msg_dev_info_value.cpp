// msg_dev_info_value.cpp

#include "msg_dev_info_value.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_DIIV"
#ifndef MSG_DIIV_LOG_EN
#define MSG_DIIV_LOG_EN true
#endif

void MsgDevInfoValue::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgDevInfoValue::create(hsys_module_id_t sender_id,
                                     hsys_module_id_t receiver_id,
                                     const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) { LOG_MSG_ERROR(MSG_DIIV_LOG_EN, "create failed"); return nullptr; }
    msg->receiver_id = receiver_id;
    MsgDevInfoValue instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgDevInfoValue::Payload MsgDevInfoValue::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload) memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
