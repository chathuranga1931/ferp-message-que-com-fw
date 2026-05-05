// msg_ota_complete_notify.cpp

#include "msg_ota_complete_notify.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaCompleteNotify::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaCompleteNotify::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaCompleteNotify instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaCompleteNotify::Payload MsgOtaCompleteNotify::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgOtaCompleteNotify::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<32> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.success    = doc["success"]    | false;
    p.last_error = static_cast<ota_fs_err_t>(doc["last_error"] | 0);
    return create(sender_id, p);
}

int32_t MsgOtaCompleteNotify::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["success"]    = p.success;
    doc["last_error"] = (int)p.last_error;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
