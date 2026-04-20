// msg_timer_start.cpp

#include "msg_timer_start.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_TMRS"
#ifndef MSG_TMRS_LOG_EN
#define MSG_TMRS_LOG_EN true
#endif

void MsgTimerStart::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgTimerStart::create(hsys_module_id_t sender_id,
                                    const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_TMRS_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgTimerStart instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgTimerStart::Payload MsgTimerStart::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgTimerStart::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.source_module_id = doc["source_module_id"].as<uint16_t>();
    p.start_offset_ms  = doc["start_offset_ms"].as<uint32_t>();
    p.duration_ms      = doc["duration_ms"].as<uint32_t>();
    p.is_repetitive    = doc["is_repetitive"].as<bool>();
    p.forced           = doc["forced"].as<bool>();
    return create(sender_id, p);
}
#endif
