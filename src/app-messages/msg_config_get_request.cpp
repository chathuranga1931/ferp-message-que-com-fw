// msg_config_get_request.cpp
//
// MsgConfigGetRequest — factory implementation.

#include "msg_config_get_request.h"
#include "hsys_log.h"

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

hsys_msg_t *MsgConfigGetRequest::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (msg == nullptr) {
        FWK_LOG_ERR("[MsgConfigGetRequest] create: hsys_msg_create failed");
    }
    // No payload to fill — payload_size is 0 in the descriptor
    return msg;
}
