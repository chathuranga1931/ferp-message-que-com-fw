// msg_mqtt_status.cpp

#include "msg_mqtt_status.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_MQTT"

void MsgMqttStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgMqttStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgMqttStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgMqttStatus::Payload MsgMqttStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
