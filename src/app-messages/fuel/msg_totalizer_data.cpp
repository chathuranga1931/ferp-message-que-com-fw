// msg_totalizer_data.cpp

#include "msg_totalizer_data.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __TAG__ "MSG_TOT "

void MsgTotalizerData::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgTotalizerData::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgTotalizerData instance(p);
    instance.serialize(msg);
    return msg;
}

MsgTotalizerData::Payload MsgTotalizerData::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgTotalizerData::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<128> doc;
    doc["nozzle_idx"]       = p.nozzle_idx;
    doc["timestamp_epoch"]  = p.timestamp_epoch;
    // Ensure NUL-termination before copying to JSON
    char safe_str[17];
    memcpy(safe_str, p.totalizer_str, 16);
    safe_str[16] = '\0';
    doc["totalizer_str"]    = safe_str;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgTotalizerData::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.nozzle_idx      = doc["nozzle_idx"]      | (uint8_t)0;
    p.timestamp_epoch = doc["timestamp_epoch"] | (uint32_t)0;
    const char *s     = doc["totalizer_str"]   | "";
    strncpy(p.totalizer_str, s, sizeof(p.totalizer_str) - 1);
    return create(sender_id, p);
}
