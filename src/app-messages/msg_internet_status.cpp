// msg_internet_status.cpp

#include "msg_internet_status.h"
#include "pal_logger.h"
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

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgInternetStatus::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);
    Payload p{};
    p.connected = doc["connected"] | false;
    return MsgInternetStatus::create(sender_id, p);
}
#endif
