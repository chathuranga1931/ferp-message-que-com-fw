// msg_config_mqtt.cpp

#include "msg_config_mqtt.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CMQT"
#ifndef MSG_CMQT_LOG_EN
#define MSG_CMQT_LOG_EN true
#endif

void MsgConfigMqtt::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigMqtt::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CMQT_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigMqtt instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigMqtt::Payload MsgConfigMqtt::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

int32_t MsgConfigMqtt::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<256> doc;
    doc["host"]     = p.host;
    doc["port"]     = p.port;
    doc["user"]     = p.user;
    doc["password"] = p.password;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgConfigMqtt::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    strncpy(p.host,     doc["host"]     | "", sizeof(p.host)     - 1);
    p.port = doc["port"] | (uint32_t)0;
    strncpy(p.user,     doc["user"]     | "", sizeof(p.user)     - 1);
    strncpy(p.password, doc["password"] | "", sizeof(p.password) - 1);
    return create(sender_id, p);
}
