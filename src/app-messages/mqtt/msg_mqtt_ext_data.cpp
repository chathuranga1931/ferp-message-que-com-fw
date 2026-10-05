// msg_mqtt_ext_data.cpp

#include "msg_mqtt_ext_data.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_MEXT"

void MsgMqttExtData::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgMqttExtData::create(hsys_module_id_t sender_id,
                                   const char *topic, size_t topic_len,
                                   const char *data,  size_t data_len)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    Payload p{};
    size_t tl = (topic && topic_len) ? topic_len : 0;
    if (tl > MSG_MQTT_EXT_TOPIC_MAX - 1) tl = MSG_MQTT_EXT_TOPIC_MAX - 1;
    if (tl) memcpy(p.topic, topic, tl);

    size_t dl = (data && data_len) ? data_len : 0;
    p.truncated = dl > MSG_MQTT_EXT_DATA_MAX - 1;
    if (p.truncated) dl = MSG_MQTT_EXT_DATA_MAX - 1;
    if (dl) memcpy(p.data, data, dl);
    p.data_len = (uint16_t)dl;

    MsgMqttExtData instance(p);
    instance.serialize(msg);
    return msg;
}

MsgMqttExtData::Payload MsgMqttExtData::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    p.topic[MSG_MQTT_EXT_TOPIC_MAX - 1] = '\0';
    p.data[MSG_MQTT_EXT_DATA_MAX - 1]   = '\0';
    return p;
}
