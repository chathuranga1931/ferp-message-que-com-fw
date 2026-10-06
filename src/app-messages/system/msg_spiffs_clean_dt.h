// msg_spiffs_clean_dt.h
//
// MsgSpiffsCleanDt — command (no payload): delete every file under the
// "esp32/" and "esp07/" SPIFFS folders (DispTap firmware images from older
// firmware) and garbage-collect SPIFFS so the space is reusable.
//
// The DispTap images now live on the SD card; the old SPIFFS copies only
// take up space.  Web pages and the config file are not touched, and the
// device does not reboot.
//
// Subscriber: ModuleSpiffs — replies DIRECT to the sender with
// MsgSpiffsCleanDtResult.  Accessible via MQTT (cmd topic).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgSpiffsCleanDt : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SPIFFS_CLEAN_DT;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,              // no payload bytes
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}

    static hsys_msg_t *create(hsys_module_id_t sender_id);

    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len);
};
