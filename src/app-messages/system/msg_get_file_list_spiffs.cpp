// msg_get_file_list_spiffs.cpp

#define __TAG__  "SPIF_GFL"  // exactly 8 chars

#include "msg_get_file_list_spiffs.h"
#include "pal_logger.h"
#include <string.h>

#ifndef MSG_GFL_LOG_EN
#define MSG_GFL_LOG_EN true
#endif

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

hsys_msg_t *MsgGetFileListSpiffs::create(hsys_module_id_t sender_id)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(MSG_GFL_LOG_EN, "create: hsys_msg_create failed");
    }
    return msg;
}

// ---------------------------------------------------------------------------
// from_json() — deserialise inbound command (payload is empty)
// ---------------------------------------------------------------------------

hsys_msg_t *MsgGetFileListSpiffs::from_json(const char * /*data_json*/,
                                             hsys_module_id_t sender_id)
{
    return create(sender_id);
}

// ---------------------------------------------------------------------------
// to_json() — echo serialisation (no fields)
// ---------------------------------------------------------------------------

int32_t MsgGetFileListSpiffs::to_json(const hsys_msg_t * /*msg*/, char *buf, uint32_t buf_len)
{
    if (!buf || buf_len < 3) return 0;
    buf[0] = '{';
    buf[1] = '}';
    buf[2] = '\0';
    return 2;
}
