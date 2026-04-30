// msg_config_get_wifi.cpp

#include "msg_config_get_wifi.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_CGWF"
#ifndef MSG_CGWF_LOG_EN
#define MSG_CGWF_LOG_EN true
#endif

void MsgConfigGetWifi::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgConfigGetWifi::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CGWF_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }
    MsgConfigGetWifi instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgConfigGetWifi::Payload MsgConfigGetWifi::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}

hsys_msg_t *MsgConfigGetWifi::mqtt_decode(const char * /*data_json*/, hsys_module_id_t sender_id)
{
    Payload p{};
    p.source_module_id = sender_id;
    return create(sender_id, p);
}
