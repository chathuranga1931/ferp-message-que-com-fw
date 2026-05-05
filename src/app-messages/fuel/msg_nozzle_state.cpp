// msg_nozzle_state.cpp

#include "msg_nozzle_state.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_NOZ "

void MsgNozzleState::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgNozzleState::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgNozzleState instance(p);
    instance.serialize(msg);
    return msg;
}

MsgNozzleState::Payload MsgNozzleState::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgNozzleState::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<64> doc;
    doc["nozzle_idx"] = p.nozzle_idx;
    doc["state"]      = (int)p.state;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgNozzleState::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, payload_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.nozzle_idx = doc["nozzle_idx"] | (uint8_t)0;
    p.state      = static_cast<nozzle_state_t>(doc["state"] | 0);
    return create(sender_id, p);
}
