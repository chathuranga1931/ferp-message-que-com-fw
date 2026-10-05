// msg_cloud_print_status.cpp

#include "msg_cloud_print_status.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_CPST"

void MsgCloudPrintStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgCloudPrintStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgCloudPrintStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgCloudPrintStatus::Payload MsgCloudPrintStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    p.queue_id[sizeof(p.queue_id) - 1]         = '\0';
    p.notify_topic[sizeof(p.notify_topic) - 1] = '\0';
    return p;
}

hsys_msg_t *MsgCloudPrintStatus::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.state             = doc["state"]             | (uint8_t)0;
    p.notify_subscribed = doc["notify_subscribed"] | false;
    p.last_http         = doc["last_http"]         | (int32_t)0;
    p.polls             = doc["polls"]             | (uint32_t)0;
    p.printed           = doc["printed"]           | (uint32_t)0;
    p.failed            = doc["failed"]            | (uint32_t)0;
    p.rejected          = doc["rejected"]          | (uint32_t)0;
    strncpy(p.queue_id,     doc["queue_id"]     | "", sizeof(p.queue_id) - 1);
    strncpy(p.notify_topic, doc["notify_topic"] | "", sizeof(p.notify_topic) - 1);
    return create(sender_id, p);
}

int32_t MsgCloudPrintStatus::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    JsonDocument doc;
    doc["state"]             = p.state;
    doc["notify_subscribed"] = p.notify_subscribed;
    doc["last_http"]         = p.last_http;
    doc["polls"]             = p.polls;
    doc["printed"]           = p.printed;
    doc["failed"]            = p.failed;
    doc["rejected"]          = p.rejected;
    doc["queue_id"]          = p.queue_id;
    doc["notify_topic"]      = p.notify_topic;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0 && w < buf_len) ? 0 : -2;
}
