// msg_config_get.cpp
//
// MsgConfigGet — factory implementation.

#include "msg_config_get.h"
#include "pal_logger.h"

#define __TAG__ "MSG_CFGG"
#ifndef MSG_CFGG_LOG_EN
#define MSG_CFGG_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigGet::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_CFGG_LOG_EN, "create: hsys_msg_create failed");
    }
    // No payload to fill — payload_size is 0 in the descriptor
    return msg;
}

#ifdef FERP_SIMULATOR
hsys_msg_t *MsgConfigGet::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}
#endif
