// msg_http_send_request.h
//
// Sent DIRECT by the session owner to ModuleHttp to trigger execution
// of the configured HTTP call.  No payload — all parameters were already
// supplied via SetUrl / SetRootCa / Header / Body messages.
// ModuleHttp responds with zero or more MsgHttpResponseHeader messages
// followed by exactly one MsgHttpResult.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"

class MsgHttpSendRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_SEND_REQUEST;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      0,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgHttpSendRequest() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}

    static hsys_msg_t *create(hsys_module_id_t sender_id);
};
