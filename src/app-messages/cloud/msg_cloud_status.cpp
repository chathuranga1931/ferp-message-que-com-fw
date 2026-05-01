// msg_cloud_status.cpp

#include "msg_cloud_status.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_CLD "

void MsgCloudStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgCloudStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgCloudStatus instance(p);
    instance.serialize(msg);
    return msg;
}

MsgCloudStatus::Payload MsgCloudStatus::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

#ifdef FERP_SIMULATOR
#include <ArduinoJson.h>
hsys_msg_t *MsgCloudStatus::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);

    Payload p{};
    const char *ev = doc["event"] | "";
    if      (strcmp(ev, "REGISTERED")      == 0) p.event = CLOUD_STATUS_REGISTERED;
    else if (strcmp(ev, "REGISTER_FAILED") == 0) p.event = CLOUD_STATUS_REGISTER_FAILED;
    else if (strcmp(ev, "PUMPED_SUCCESS")  == 0) p.event = CLOUD_STATUS_PUMPED_SUCCESS;
    else if (strcmp(ev, "PUMPED_FAILED")   == 0) p.event = CLOUD_STATUS_PUMPED_FAILED;
    else if (strcmp(ev, "HB_SENT")         == 0) p.event = CLOUD_STATUS_HB_SENT;
    else if (strcmp(ev, "HB_FAILED")       == 0) p.event = CLOUD_STATUS_HB_FAILED;

    p.nozzle_idx = (uint8_t)doc["nozzle_idx"].as<int>();

    return MsgCloudStatus::create(sender_id, p);
}
#endif
