// msg_ota_request_driver.cpp

#include "msg_ota_request_driver.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_OTA "

void MsgOtaRequestDriver::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgOtaRequestDriver::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgOtaRequestDriver instance;
    instance.serialize(msg);
    return msg;
}

hsys_msg_t *MsgOtaRequestDriver::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}

int32_t MsgOtaRequestDriver::to_json(const hsys_msg_t * /*msg*/, char *data_json, uint32_t buf_len)
{
    if (buf_len >= 3) { data_json[0] = '{'; data_json[1] = '}'; data_json[2] = '\0'; }
    return 0;
}
