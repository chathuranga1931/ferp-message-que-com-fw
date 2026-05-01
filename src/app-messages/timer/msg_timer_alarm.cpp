// msg_timer_alarm.cpp

#include "msg_timer_alarm.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_TALM"
#ifndef MSG_TALM_LOG_EN
#define MSG_TALM_LOG_EN true
#endif

void MsgTimerAlarm::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgTimerAlarm::create(hsys_module_id_t sender_id,
                                    const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_TALM_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgTimerAlarm instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgTimerAlarm::Payload MsgTimerAlarm::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgTimerAlarm::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.source_module_id = doc["source_module_id"].as<uint16_t>();
    p.elapsed_ms       = doc["elapsed_ms"].as<uint32_t>();
    return create(sender_id, p);
}
#endif
