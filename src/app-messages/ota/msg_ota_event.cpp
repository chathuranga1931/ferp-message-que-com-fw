// msg_ota_event.cpp

#include "msg_ota_event.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaEvent::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaEvent::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaEvent instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaEvent::Payload MsgOtaEvent::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgOtaEvent::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<96> doc;
    doc["event"]      = (int)p.event;
    doc["target_idx"] = p.target_idx;
    doc["version"]    = p.version;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgOtaEvent::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<96> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.event      = static_cast<ota_event_id_t>(doc["event"] | 0);
    p.target_idx = doc["target_idx"] | (uint8_t)0;
    strncpy(p.version, doc["version"] | "", sizeof(p.version) - 1);
    return create(sender_id, p);
}
