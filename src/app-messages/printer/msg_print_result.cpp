// msg_print_result.cpp

#include "msg_print_result.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_PRES"

void MsgPrintResult::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgPrintResult::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgPrintResult instance(p);
    instance.serialize(msg);
    return msg;
}

MsgPrintResult::Payload MsgPrintResult::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgPrintResult::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.job_id      = doc["job_id"]      | (uint32_t)0;
    p.result      = doc["result"]      | (uint8_t)0;
    p.source      = doc["source"]      | (uint8_t)0;
    p.print_count = doc["print_count"] | (uint32_t)0;
    return create(sender_id, p);
}

int32_t MsgPrintResult::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    JsonDocument doc;
    doc["job_id"]      = p.job_id;
    doc["result"]      = p.result;
    doc["source"]      = p.source;
    doc["print_count"] = p.print_count;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0 && w < buf_len) ? 0 : -2;
}
