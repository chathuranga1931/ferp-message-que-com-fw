// msg_config_get_dt.cpp

#include "msg_config_get_dt.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_CGDT"
#ifndef MSG_CGDT_LOG_EN
#define MSG_CGDT_LOG_EN true
#endif

void MsgConfigGetDT::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigGetDT::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CGDT_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigGetDT instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigGetDT::Payload MsgConfigGetDT::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgConfigGetDT::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.source_module_id = doc["source_module_id"].as<uint16_t>();
    return create(sender_id, p);
}
#endif
