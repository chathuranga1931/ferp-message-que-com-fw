// msg_system_reboot.cpp

#include "msg_system_reboot.h"
#include "pal_logger.h"

#define __TAG__ "MSG_REBO"
#ifndef MSG_REBO_LOG_EN
#define MSG_REBO_LOG_EN true
#endif

hsys_msg_t *MsgSystemReboot::create(hsys_module_id_t sender_id, const Payload &)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_REBO_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}

hsys_msg_t *MsgSystemReboot::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}

int32_t MsgSystemReboot::to_json(const hsys_msg_t * /*msg*/, char *data_json, uint32_t buf_len)
{
    if (buf_len >= 3) { data_json[0] = '{'; data_json[1] = '}'; data_json[2] = '\0'; }
    return 0;
}
