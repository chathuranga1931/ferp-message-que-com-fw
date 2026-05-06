// msg_http_body_response.cpp

#include "msg_http_body_response.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

void MsgHttpBodyResponse::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgHttpBodyResponse::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "BodyResponse: pool full");
        return nullptr;
    }
    MsgHttpBodyResponse instance(p);
    instance.serialize(msg);
    return msg;
}

MsgHttpBodyResponse::Payload MsgHttpBodyResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
