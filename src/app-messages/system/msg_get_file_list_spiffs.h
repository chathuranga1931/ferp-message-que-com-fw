// msg_get_file_list_spiffs.h
//
// MsgGetFileListSpiffs — zero-payload command that asks ModuleSysmon to
// enumerate all files on SPIFFS and reply with MsgFileListSpiffs.
//
// Accessible via MQTT (inbound command) and WebAPI (request half of the
// getFileListSpiffs / fileListSpiffs round-trip).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgGetFileListSpiffs : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_GET_FILE_LIST_SPIFFS;

    // No payload — the command carries no data.
    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}

    /** Create a zero-payload command message. */
    static hsys_msg_t *create(hsys_module_id_t sender_id);

    /** JSON codec — command deserialised from MQTT / WebAPI. */
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);

    /** JSON codec — serialise for echo / logging (outputs `{}`). */
    static int32_t to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len);
};
