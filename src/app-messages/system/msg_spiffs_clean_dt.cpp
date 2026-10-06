// msg_spiffs_clean_dt.cpp

#define __TAG__  "MSG_SCDT"  // exactly 8 chars

#include "msg_spiffs_clean_dt.h"
#include "pal_logger.h"

#ifndef MSG_SCDT_LOG_EN
#define MSG_SCDT_LOG_EN true
#endif

hsys_msg_t *MsgSpiffsCleanDt::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_SCDT_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}

hsys_msg_t *MsgSpiffsCleanDt::from_json(const char * /*data_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}

int32_t MsgSpiffsCleanDt::to_json(const hsys_msg_t * /*msg*/, char *buf, uint32_t buf_len)
{
    if (!buf || buf_len < 3) return 0;
    buf[0] = '{';
    buf[1] = '}';
    buf[2] = '\0';
    return 2;
}
