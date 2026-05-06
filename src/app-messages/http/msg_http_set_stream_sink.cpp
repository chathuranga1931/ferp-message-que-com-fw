// msg_http_set_stream_sink.cpp

#include "msg_http_set_stream_sink.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_HTTP"

void MsgHttpSetStreamSink::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgHttpSetStreamSink::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "SetStreamSink: pool full");
        return nullptr;
    }
    MsgHttpSetStreamSink instance(p);
    instance.serialize(msg);
    return msg;
}

MsgHttpSetStreamSink::Payload MsgHttpSetStreamSink::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}
