// msg_timer_stop_response.cpp

#include "msg_timer_stop_response.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_TXRP"
#ifndef MSG_TXRP_LOG_EN
#define MSG_TXRP_LOG_EN true
#endif

void MsgTimerStopResponse::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgTimerStopResponse::create(hsys_module_id_t sender_id,
                                           const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_TXRP_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgTimerStopResponse instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgTimerStopResponse::Payload MsgTimerStopResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgTimerStopResponse::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.source_module_id = doc["source_module_id"].as<uint16_t>();
    p.result           = static_cast<timer_result_t>(doc["result"].as<uint8_t>());
    return create(sender_id, p);
}
#endif
