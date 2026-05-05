// msg_ota_start_request.cpp

#include "msg_ota_start_request.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaStartRequest::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaStartRequest::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaStartRequest instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaStartRequest::Payload MsgOtaStartRequest::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgOtaStartRequest::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<96> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.target_idx = doc["target_idx"] | (uint8_t)0;
    strncpy(p.incoming_version, doc["incoming_version"] | "", sizeof(p.incoming_version) - 1);
    return create(sender_id, p);
}

int32_t MsgOtaStartRequest::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<96> doc;
    doc["target_idx"]       = p.target_idx;
    doc["incoming_version"] = p.incoming_version;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
