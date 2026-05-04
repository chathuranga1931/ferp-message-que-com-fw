// msg_dev_info_write.cpp

#include "msg_dev_info_write.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_DIIW"
#ifndef MSG_DIIW_LOG_EN
#define MSG_DIIW_LOG_EN true
#endif

void MsgDevInfoWrite::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

hsys_msg_t *MsgDevInfoWrite::create(hsys_module_id_t sender_id, const Payload &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) { LOG_MSG_ERROR(MSG_DIIW_LOG_EN, "create failed"); return nullptr; }
    MsgDevInfoWrite instance(payload);
    instance.serialize(msg);
    return msg;
}

MsgDevInfoWrite::Payload MsgDevInfoWrite::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload) memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

hsys_msg_t *MsgDevInfoWrite::create_str(hsys_module_id_t sender_id, uint16_t key, const char *str)
{
    Payload p{};
    p.key  = key;
    p.type = HSYS_TYPE_STRING;
    strncpy(p.value.as_str, str, sizeof(p.value.as_str) - 1);
    return create(sender_id, p);
}

hsys_msg_t *MsgDevInfoWrite::create_u32(hsys_module_id_t sender_id, uint16_t key, uint32_t val)
{
    Payload p{};
    p.key              = key;
    p.type             = HSYS_TYPE_UINT32;
    p.value.as_uint32  = val;
    return create(sender_id, p);
}

hsys_msg_t *MsgDevInfoWrite::create_bool(hsys_module_id_t sender_id, uint16_t key, bool val)
{
    Payload p{};
    p.key           = key;
    p.type          = HSYS_TYPE_BOOL;
    p.value.as_bool = val;
    return create(sender_id, p);
}
