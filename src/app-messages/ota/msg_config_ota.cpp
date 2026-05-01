// msg_config_ota.cpp

#include "msg_config_ota.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_COTA"
#ifndef MSG_COTA_LOG_EN
#define MSG_COTA_LOG_EN true
#endif

void MsgConfigOta::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigOta::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_COTA_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigOta instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigOta::Payload MsgConfigOta::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

int32_t MsgConfigOta::mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<256> doc;
    doc["server_url"]        = p.server_url;
    doc["check_interval_s"] = p.check_interval_s;
    // root_ca is intentionally omitted (sensitive, potentially large)
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
