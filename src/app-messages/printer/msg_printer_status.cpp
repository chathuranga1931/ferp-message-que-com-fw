// msg_printer_status.cpp

#include "msg_printer_status.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_PSTA"

void MsgPrinterStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgPrinterStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgPrinterStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgPrinterStatus::Payload MsgPrinterStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    p.transport[sizeof(p.transport) - 1] = '\0';
    p.detail[sizeof(p.detail) - 1]       = '\0';
    return p;
}

hsys_msg_t *MsgPrinterStatus::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    strncpy(p.transport, doc["transport"] | "", sizeof(p.transport) - 1);
    strncpy(p.detail,    doc["detail"]    | "", sizeof(p.detail) - 1);
    p.ready       = doc["ready"]       | false;
    p.last_result = doc["last_result"] | (uint8_t)0;
    p.last_cmd    = doc["last_cmd"]    | (uint8_t)0xFF;
    p.print_count = doc["print_count"] | (uint32_t)0;
    p.jobs_ok     = doc["jobs_ok"]     | (uint32_t)0;
    p.jobs_failed = doc["jobs_failed"] | (uint32_t)0;
    p.last_job_id = doc["last_job_id"] | (uint32_t)0;
    return create(sender_id, p);
}

int32_t MsgPrinterStatus::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    JsonDocument doc;
    doc["transport"]   = p.transport;
    doc["ready"]       = p.ready;
    doc["last_result"] = p.last_result;
    doc["last_cmd"]    = p.last_cmd;
    doc["print_count"] = p.print_count;
    doc["jobs_ok"]     = p.jobs_ok;
    doc["jobs_failed"] = p.jobs_failed;
    doc["last_job_id"] = p.last_job_id;
    doc["detail"]      = p.detail;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0 && w < buf_len) ? 0 : -2;
}
