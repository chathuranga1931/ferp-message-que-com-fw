// msg_internet_status.cpp

#include "msg_internet_status.h"
#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>

#define __TAG__ "MSG_INET"

void MsgInternetStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgInternetStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgInternetStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgInternetStatus::Payload MsgInternetStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

int32_t MsgInternetStatus::mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    StaticJsonDocument<32> doc;
    doc["connected"] = p.connected;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}

hsys_msg_t *MsgInternetStatus::mqtt_decode(const char *data_json, hsys_module_id_t sender_id)
{
    StaticJsonDocument<32> doc;
    if (deserializeJson(doc, data_json) != DeserializationError::Ok) return nullptr;
    Payload p{};
    p.connected = doc["connected"] | false;
    return create(sender_id, p);
}
