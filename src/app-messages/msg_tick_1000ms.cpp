// msg_tick_1000ms.cpp
//
// MsgTick1000ms — factory implementation (no payload to serialize).

#include "msg_tick_1000ms.h"
#include "pal_logger.h"

#define __TAG__ "MSG_TICK"
#ifndef MSG_TICK_LOG_EN
#define MSG_TICK_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgTick1000ms::create(hsys_module_id_t sender_id,
                                   const Payload & /*unused*/)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        LOG_MSG_ERROR(MSG_TICK_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;  // nothing to serialize — descriptor payload_size == 0
}

#ifdef FERP_SIMULATOR
hsys_msg_t *MsgTick1000ms::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id, Payload{});
}
#endif
