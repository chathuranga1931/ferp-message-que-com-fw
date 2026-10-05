// msg_cloud_print_get_status.cpp

#include "msg_cloud_print_get_status.h"
#include "pal_logger.h"
#include <string.h>
#include <stdio.h>

#define __TAG__ "MSG_CPGS"

void MsgCloudPrintGetStatus::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgCloudPrintGetStatus::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgCloudPrintGetStatus instance(p);
    instance.serialize(msg);
    return msg;
}

hsys_msg_t *MsgCloudPrintGetStatus::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}

int32_t MsgCloudPrintGetStatus::to_json(const hsys_msg_t * /*msg*/, char *data_json, uint32_t buf_len)
{
    int n = snprintf(data_json, buf_len, "{}");
    return (n > 0 && (uint32_t)n < buf_len) ? 0 : -2;
}
