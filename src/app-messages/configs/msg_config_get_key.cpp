// msg_config_get_key.cpp

#include "msg_config_get_key.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CGKY"
#ifndef MSG_CGKY_LOG_EN
#define MSG_CGKY_LOG_EN true
#endif

void MsgConfigGetKey::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigGetKey::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_CGKY_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigGetKey inst(payload);
    inst.serialize(msg);
    return msg;
}

MsgConfigGetKey::Payload MsgConfigGetKey::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

// JSON wire format: {"key": 4097}   (decimal or hex uint)
hsys_msg_t *MsgConfigGetKey::from_json(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.key              = (uint16_t)(doc["key"] | 0);
    p.source_module_id = sender_id;
    return create(sender_id, p);
}

int32_t MsgConfigGetKey::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<64> doc;
    doc["key"] = p.key;
    size_t w = serializeJson(doc, data_json_out, buf_len);
    return (w > 0) ? 0 : -2;
}
