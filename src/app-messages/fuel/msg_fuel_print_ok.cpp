// msg_fuel_print_ok.cpp

#include "msg_fuel_print_ok.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_PROK"

void MsgFuelPrintOk::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgFuelPrintOk::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgFuelPrintOk instance(p);
    instance.serialize(msg);
    return msg;
}

MsgFuelPrintOk::Payload MsgFuelPrintOk::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgFuelPrintOk::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<128> doc;
    doc["nozzle_idx"]         = p.nozzle_idx;
    doc["dispenser_event_id"] = p.dispenser_event_id;
    doc["timestamp_epoch"]    = (long long)p.timestamp_epoch;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgFuelPrintOk::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.nozzle_idx         = doc["nozzle_idx"]         | (uint8_t)0;
    p.dispenser_event_id = doc["dispenser_event_id"] | (uint32_t)0;
    p.timestamp_epoch    = doc["timestamp_epoch"]    | (long long)0;
    return create(sender_id, p);
}
