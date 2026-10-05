// msg_mqtt_subscribe.cpp

#include "msg_mqtt_subscribe.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_MSUB"

void MsgMqttSubscribe::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgMqttSubscribe::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    Payload safe = p;
    safe.topic[MSG_MQTT_SUB_TOPIC_MAX - 1] = '\0';
    MsgMqttSubscribe instance(safe);
    instance.serialize(msg);
    return msg;
}

MsgMqttSubscribe::Payload MsgMqttSubscribe::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    p.topic[MSG_MQTT_SUB_TOPIC_MAX - 1] = '\0';
    return p;
}
