// msg_sensor_data.cpp
//
// MsgSensorData — serialize / deserialize / factory implementation.

#include "msg_sensor_data.h"
#include "pal_logger.h"
#include <ArduinoJson.h>

#define __TAG__ "MSG_SENS"
#ifndef MSG_SENS_LOG_EN
#define MSG_SENS_LOG_EN true
#endif

#include <string.h>

// ---------------------------------------------------------------------------
// IHsysMsg::serialize  — copy Payload into the framework-allocated buffer
// ---------------------------------------------------------------------------

void MsgSensorData::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgSensorData::create(hsys_module_id_t sender_id,
                                   const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_SENS_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }

    MsgSensorData instance(payload);
    instance.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// Static deserializer
// ---------------------------------------------------------------------------

MsgSensorData::Payload MsgSensorData::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

int32_t MsgSensorData::mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<64> doc;
    doc["counter"]     = p.counter;
    doc["temperature"] = p.temperature;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgSensorData::mqtt_decode(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.counter     = doc["counter"]     | (uint32_t)0;
    p.temperature = doc["temperature"] | 0.0f;
    return create(sender_id, p);
}
