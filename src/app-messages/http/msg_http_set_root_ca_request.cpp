// msg_http_set_root_ca_request.cpp

#include "msg_http_set_root_ca_request.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

void MsgHttpSetRootCaRequest::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgHttpSetRootCaRequest::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "SetRootCaRequest: pool full");
        return nullptr;
    }
    MsgHttpSetRootCaRequest instance(p);
    instance.serialize(msg);
    return msg;
}

MsgHttpSetRootCaRequest::Payload MsgHttpSetRootCaRequest::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
