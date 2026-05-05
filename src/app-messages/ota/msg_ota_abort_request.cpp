// msg_ota_abort_request.cpp

#include "msg_ota_abort_request.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaAbortRequest::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaAbortRequest::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaAbortRequest instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaAbortRequest::Payload MsgOtaAbortRequest::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgOtaAbortRequest::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<32> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.reason = static_cast<ota_abort_reason_t>(doc["reason"] | 0);
    return create(sender_id, p);
}

int32_t MsgOtaAbortRequest::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["reason"] = (int)p.reason;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
