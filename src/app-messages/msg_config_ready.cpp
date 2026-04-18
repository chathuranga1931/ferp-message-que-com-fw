// msg_config_ready.cpp
//
// MsgConfigReady — serialize / deserialize / factory implementation.

#include "msg_config_ready.h"
#include "pal_logger.h"

#define __TAG__ "MSG_CFGR"
#ifndef MSG_CFGR_LOG_EN
#define MSG_CFGR_LOG_EN true
#endif

#include <string.h>

// ---------------------------------------------------------------------------
// IHsysMsg::serialize
// ---------------------------------------------------------------------------

void MsgConfigReady::serialize(hsys_msg_t *msg) const
{
    if (msg == nullptr || msg->payload == nullptr) return;
    memcpy(msg->payload, &m_payload, sizeof(Payload));
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigReady::create(hsys_module_id_t    sender_id,
                                    const app_config_t *config)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CFGR_LOG_EN, "create: hsys_msg_create failed");
        return nullptr;
    }

    MsgConfigReady instance(Payload{ .config = config });
    instance.serialize(msg);
    return msg;
}

// ---------------------------------------------------------------------------
// Static deserializer
// ---------------------------------------------------------------------------

MsgConfigReady::Payload MsgConfigReady::deserialize(const hsys_msg_t &msg)
{
    Payload p{ .config = nullptr };
    if (msg.payload != nullptr && msg.payload_size >= sizeof(Payload)) {
        memcpy(&p, msg.payload, sizeof(Payload));
    }
    return p;
}
