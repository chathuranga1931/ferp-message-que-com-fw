// msg_dev_info_value.cpp

#include "msg_dev_info_value.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_DIIV"
#ifndef MSG_DIIV_LOG_EN
#define MSG_DIIV_LOG_EN true
#endif

void MsgDevInfoValue::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgDevInfoValue::create(hsys_module_id_t sender_id,
                                     hsys_module_id_t receiver_id,
                                     const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) { LOG_MSG_ERROR(MSG_DIIV_LOG_EN, "create failed"); return nullptr; }
    msg->receiver_id = receiver_id;
    MsgDevInfoValue instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgDevInfoValue::Payload MsgDevInfoValue::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload) memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

// JSON wire format:
// from_json: {"key": 40961, "type": 1, "is_valid": true, "value": "some-string"}
// to_json:   {"key": 40961, "type": 1, "is_valid": true, "value": "some-string"}
hsys_msg_t *MsgDevInfoValue::from_json(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.key      = (uint16_t)(doc["key"]      | 0);
    p.type     = (hsys_type_t)(uint8_t)(doc["type"] | 0);
    p.is_valid = doc["is_valid"] | false;
    const char *v = doc["value"] | "";
    strncpy(p.value.as_str, v, STR_MAX_LEN - 1);
    return create(sender_id, sender_id, p);
}

int32_t MsgDevInfoValue::to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<192> doc;
    doc["key"]      = p.key;
    doc["type"]     = (uint8_t)p.type;
    doc["is_valid"] = p.is_valid;
    // Always serialize as string; numeric types can be decoded by the client
    doc["value"]    = p.value.as_str;
    size_t w = serializeJson(doc, data_json_out, buf_len);
    return (w > 0) ? 0 : -2;
}
