// msg_config_get_mqtt.cpp

#include "msg_config_get_mqtt.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CGMQ"
#ifndef MSG_CGMQ_LOG_EN
#define MSG_CGMQ_LOG_EN true
#endif

void MsgConfigGetMqtt::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigGetMqtt::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CGMQ_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigGetMqtt instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigGetMqtt::Payload MsgConfigGetMqtt::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

hsys_msg_t *MsgConfigGetMqtt::from_json(const char * /*data_json*/, hsys_module_id_t sender_id)
{
    Payload p{};
    p.source_module_id = sender_id;
    return create(sender_id, p);
}

int32_t MsgConfigGetMqtt::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["source_module_id"] = (int)p.source_module_id;
    size_t w = serializeJson(doc, data_json_out, buf_len);
    return (w > 0) ? 0 : -2;
}
