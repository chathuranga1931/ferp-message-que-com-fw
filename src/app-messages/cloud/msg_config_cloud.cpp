// msg_config_cloud.cpp

#include "msg_config_cloud.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CCLD"
#ifndef MSG_CCLD_LOG_EN
#define MSG_CCLD_LOG_EN true
#endif

void MsgConfigCloud::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigCloud::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CCLD_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigCloud instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigCloud::Payload MsgConfigCloud::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

int32_t MsgConfigCloud::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<128> doc;
    doc["hb_enabled"]    = p.hb_enabled;
    doc["hb_interval_s"] = p.hb_interval_s;
    // root_ca is intentionally omitted (sensitive, potentially large)
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgConfigCloud::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.root_ca       = nullptr;  // can't transfer pointer via JSON
    p.hb_enabled    = doc["hb_enabled"]    | false;
    p.hb_interval_s = doc["hb_interval_s"] | (uint32_t)0;
    return create(sender_id, p);
}
