// msg_fuel_totalizer.cpp

#include "msg_fuel_totalizer.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __TAG__ "MSG_FUEL"

void MsgFuelTotalizer::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgFuelTotalizer::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgFuelTotalizer instance(p);
    instance.serialize(msg);
    return msg;
}

MsgFuelTotalizer::Payload MsgFuelTotalizer::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgFuelTotalizer::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<160> doc;
    doc["nozzle_idx"] = p.nozzle_idx;
    doc["time_stamp"] = p.time_stamp;
    // vol_lx1000 is uint64 — encode as string to avoid JSON integer overflow
    char vol_str[24];
    snprintf(vol_str, sizeof(vol_str), "%llu", (unsigned long long)p.vol_lx1000);
    doc["vol_lx1000"] = vol_str;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgFuelTotalizer::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.nozzle_idx = doc["nozzle_idx"] | (uint8_t)0;
    p.time_stamp = doc["time_stamp"] | (uint32_t)0;
    const char *vol_str = doc["vol_lx1000"] | "0";
    p.vol_lx1000 = (uint64_t)strtoull(vol_str, nullptr, 10);
    return create(sender_id, p);
}
