// msg_http_abort_request.h
//
// Sent DIRECT by the session owner to ModuleHttp to cancel the session
// before MsgHttpSendRequest has been issued.  The session is released
// immediately; no MsgHttpResult is sent.
// Sending this after MsgHttpSendRequest (while EXECUTING) is not supported
// in this revision — the caller must wait for MsgHttpResult.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"

class MsgHttpAbortRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_ABORT_REQUEST;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      0,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgHttpAbortRequest() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}

    static hsys_msg_t *create(hsys_module_id_t sender_id);
};
