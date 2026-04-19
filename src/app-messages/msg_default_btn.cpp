// msg_default_btn.cpp
//
// MsgDefaultBtn — serialize / deserialize / factory implementation.

#include "msg_default_btn.h"
#include "pal_logger.h"

#include <string.h>

#define __TAG__ "MSG_DBTN"
#ifndef MSG_DBTN_LOG_EN
#define MSG_DBTN_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// IHsysMsg::serialize
// ---------------------------------------------------------------------------

void MsgDefaultBtn::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgDefaultBtn::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_DBTN_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgDefaultBtn instance(payload);
    instance.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// Static deserializer
// ---------------------------------------------------------------------------

MsgDefaultBtn::Payload MsgDefaultBtn::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgDefaultBtn::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.status = static_cast<btn_press_t>(doc["status"].as<uint8_t>());
    return create(sender_id, p);
}
#endif
