// msg_dev_info_read.cpp

#include "msg_dev_info_read.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_DIIR"
#ifndef MSG_DIIR_LOG_EN
#define MSG_DIIR_LOG_EN true
#endif

void MsgDevInfoRead::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgDevInfoRead::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) { LOG_MSG_ERROR(MSG_DIIR_LOG_EN, "create failed"); return nullptr; }
    MsgDevInfoRead instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgDevInfoRead::Payload MsgDevInfoRead::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload) memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

// JSON wire format: {"key": 40961, "source_module_id": 0}
hsys_msg_t *MsgDevInfoRead::from_json(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.key              = (uint16_t)(doc["key"] | 0);
    p.source_module_id = sender_id;
    return create(sender_id, p);
}

int32_t MsgDevInfoRead::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<64> doc;
    doc["key"]              = p.key;
    doc["source_module_id"] = (uint8_t)p.source_module_id;
    size_t w = serializeJson(doc, data_json_out, buf_len);
    return (w > 0) ? 0 : -2;
}
