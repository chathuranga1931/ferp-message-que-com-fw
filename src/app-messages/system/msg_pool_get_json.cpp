// msg_pool_get_json.cpp

#include "msg_pool_get_json.h"
#include "pal_logger.h"

#define __TAG__ "POOL_GJN"
#ifndef MSG_PGJ_LOG_EN
#define MSG_PGJ_LOG_EN true
#endif

hsys_msg_t *MsgPoolGetJson::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_PGJ_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}

hsys_msg_t *MsgPoolGetJson::from_json(const char * /*data_json*/,
                                       hsys_module_id_t sender_id)
{
    return create(sender_id);
}

int32_t MsgPoolGetJson::to_json(const hsys_msg_t * /*msg*/,
                                 char *data_json, uint32_t buf_len)
{
    if (buf_len >= 3) { data_json[0] = '{'; data_json[1] = '}'; data_json[2] = '\0'; }
    return 0;
}
