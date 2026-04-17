// msg_tick_1000ms.cpp
//
// MsgTick1000ms — factory implementation (no payload to serialize).

#include "msg_tick_1000ms.h"
#include "hsys_log.h"

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgTick1000ms::create(hsys_module_id_t sender_id,
                                   const Payload & /*unused*/)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        FWK_LOG_ERR("[MsgTick1000ms] create: hsys_msg_create failed");
    }
    return msg;  // nothing to serialize — descriptor payload_size == 0
}
