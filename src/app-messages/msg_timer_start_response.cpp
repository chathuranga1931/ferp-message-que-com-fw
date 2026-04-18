// msg_timer_start_response.cpp

#include "msg_timer_start_response.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_TSRP"
#ifndef MSG_TSRP_LOG_EN
#define MSG_TSRP_LOG_EN true
#endif

void MsgTimerStartResponse::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgTimerStartResponse::create(hsys_module_id_t sender_id,
                                            const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_TSRP_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgTimerStartResponse instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgTimerStartResponse::Payload MsgTimerStartResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}
