// msg_http_start_response.cpp

#include "msg_http_start_response.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

void MsgHttpStartResponse::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgHttpStartResponse::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "StartResponse: pool full");
        return nullptr;
    }
    MsgHttpStartResponse instance(p);
    instance.serialize(msg);
    return msg;
}

MsgHttpStartResponse::Payload MsgHttpStartResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
