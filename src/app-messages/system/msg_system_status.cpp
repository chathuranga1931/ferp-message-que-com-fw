// msg_system_status.cpp

#include "msg_system_status.h"
#include "pal_logger.h"
#include <string.h>
#include <ArduinoJson.h>

#define __TAG__ "MSG_SYSS"

void MsgSystemStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgSystemStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgSystemStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgSystemStatus::Payload MsgSystemStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgSystemStatus::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);

    Payload p{};
    const char *st = doc["status"] | "IDLE";
    if      (strcmp(st, "BUSY")     == 0) p.status = SYSTEM_STATUS_BUSY;
    else if (strcmp(st, "MODERATE") == 0) p.status = SYSTEM_STATUS_MODERATE;
    else                                  p.status = SYSTEM_STATUS_IDLE;

    return MsgSystemStatus::create(sender_id, p);
}

int32_t MsgSystemStatus::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    static const char *status_names[] = { "IDLE", "BUSY", "MODERATE" };
    auto p = deserialize(*msg);
    StaticJsonDocument<64> doc;
    doc["status"] = ((unsigned)p.status < 3) ? status_names[p.status] : "UNKNOWN";
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
