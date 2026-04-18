// msg_config_ready.cpp
//
// MsgConfigReady — factory implementation.

#include "msg_config_ready.h"
#include "pal_logger.h"

#define __TAG__ "MSG_CFGR"
#ifndef MSG_CFGR_LOG_EN
#define MSG_CFGR_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigReady::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CFGR_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}
