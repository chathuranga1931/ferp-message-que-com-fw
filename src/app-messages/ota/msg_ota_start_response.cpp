// msg_ota_start_response.cpp

#include "msg_ota_start_response.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaStartResponse::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaStartResponse::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaStartResponse instance(p);
    instance.serialize(msg);
    return msg;
}

MsgOtaStartResponse::Payload MsgOtaStartResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgOtaStartResponse::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<32> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.result = static_cast<ota_start_result_t>(doc["result"] | 0);
    return create(sender_id, p);
}

int32_t MsgOtaStartResponse::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["result"] = (int)p.result;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
