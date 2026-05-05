// msg_config_wifi.cpp

#include "msg_config_wifi.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CWIF"
#ifndef MSG_CWIF_LOG_EN
#define MSG_CWIF_LOG_EN true
#endif

void MsgConfigWifi::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigWifi::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CWIF_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigWifi instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigWifi::Payload MsgConfigWifi::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

int32_t MsgConfigWifi::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<192> doc;
    doc["ssid"]     = p.ssid;
    doc["password"] = p.password;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgConfigWifi::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    strncpy(p.ssid,     doc["ssid"]     | "", sizeof(p.ssid)     - 1);
    strncpy(p.password, doc["password"] | "", sizeof(p.password) - 1);
    return create(sender_id, p);
}
