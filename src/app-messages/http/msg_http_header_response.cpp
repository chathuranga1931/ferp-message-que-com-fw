// msg_http_header_response.cpp

#include "msg_http_header_response.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

void MsgHttpHeaderResponse::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgHttpHeaderResponse::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "HeaderResponse: pool full");
        return nullptr;
    }
    MsgHttpHeaderResponse instance(p);
    instance.serialize(msg);
    return msg;
}

MsgHttpHeaderResponse::Payload MsgHttpHeaderResponse::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
