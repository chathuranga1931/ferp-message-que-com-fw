// msg_config_set.cpp
//
// MsgConfigSet — serialize / deserialize / factory implementation.

#include "msg_config_set.h"
#include "pal_logger.h"

#define __TAG__ "MSG_CFGS"
#ifndef MSG_CFGS_LOG_EN
#define MSG_CFGS_LOG_EN true
#endif

#include <string.h>

// ---------------------------------------------------------------------------
// IHsysMsg::serialize
// ---------------------------------------------------------------------------

void MsgConfigSet::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// Static factory — generic
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create(hsys_module_id_t sender_id,
                                   const Payload   &payload)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CFGS_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }

    MsgConfigSet instance(payload);
    instance.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// Convenience factory — string
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create_str(hsys_module_id_t sender_id,
                                      const char      *key,
                                      const char      *value)
{
    Payload p{};
    strncpy(p.key, key, KEY_MAX_LEN - 1);
    p.type = APP_CFG_TYPE_STRING;
    strncpy(p.value.as_str, value, STR_MAX_LEN - 1);
    return create(sender_id, p);
}

// ---------------------------------------------------------------------------
// Convenience factory — uint32
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create_uint32(hsys_module_id_t sender_id,
                                         const char      *key,
                                         uint32_t         value)
{
    Payload p{};
    strncpy(p.key, key, KEY_MAX_LEN - 1);
    p.type           = APP_CFG_TYPE_UINT32;
    p.value.as_uint32 = value;
    return create(sender_id, p);
}

// ---------------------------------------------------------------------------
// Convenience factory — bool
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigSet::create_bool(hsys_module_id_t sender_id,
                                       const char      *key,
                                       bool             value)
{
    Payload p{};
    strncpy(p.key, key, KEY_MAX_LEN - 1);
    p.type          = APP_CFG_TYPE_BOOL;
    p.value.as_bool = value;
    return create(sender_id, p);
}

// ---------------------------------------------------------------------------
// Static deserializer
// ---------------------------------------------------------------------------

MsgConfigSet::Payload MsgConfigSet::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}
