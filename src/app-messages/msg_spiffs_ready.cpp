// msg_spiffs_ready.cpp

#include "msg_spiffs_ready.h"
#include "pal_logger.h"

#define __TAG__ "MSG_SPIF"
#ifndef MSG_SPIF_LOG_EN
#define MSG_SPIF_LOG_EN true
#endif

hsys_msg_t *MsgSpiffsReady::create(hsys_module_id_t sender_id, const Payload &)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_SPIF_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}

#ifdef FERP_SIMULATOR
hsys_msg_t *MsgSpiffsReady::from_json(const char * /*payload_json*/, hsys_module_id_t sender_id)
{
    return create(sender_id);
}
#endif
