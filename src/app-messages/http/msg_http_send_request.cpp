// msg_http_send_request.cpp

#include "msg_http_send_request.h"
#include "pal_logger.h"

#define __TAG__ "MSG_HTTP"

hsys_msg_t *MsgHttpSendRequest::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "SendRequest: pool full");
    }
    return msg;
}
