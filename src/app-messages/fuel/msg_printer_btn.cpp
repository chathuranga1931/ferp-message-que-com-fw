// msg_printer_btn.cpp
//
// MsgPrinterBtn — serialize / deserialize / factory implementation.

#include "msg_printer_btn.h"
#include "pal_logger.h"

#include <string.h>

#define __TAG__ "MSG_PBTN"
#ifndef MSG_PBTN_LOG_EN
#define MSG_PBTN_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// IHsysMsg::serialize
// ---------------------------------------------------------------------------

void MsgPrinterBtn::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgPrinterBtn::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_PBTN_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgPrinterBtn instance(payload);
    instance.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// Static deserializer
// ---------------------------------------------------------------------------

MsgPrinterBtn::Payload MsgPrinterBtn::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

#include <ArduinoJson.h>
hsys_msg_t *MsgPrinterBtn::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.button_id = doc["button_id"].as<uint8_t>();
    p.status    = static_cast<btn_press_t>(doc["status"].as<uint8_t>());
    return create(sender_id, p);
}

int32_t MsgPrinterBtn::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["button_id"] = p.button_id;
    doc["status"]    = (uint8_t)p.status;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
