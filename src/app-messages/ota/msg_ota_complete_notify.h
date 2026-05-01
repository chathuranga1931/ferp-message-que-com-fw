// msg_ota_complete_notify.h
//
// Sent DIRECT by the OTA source to OtaModule after the last fclose() call,
// signalling that the binary write is done (success or failure).

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "FileSystemDriver.h"   // ota_fs_err_t
#include <stdint.h>

class MsgOtaCompleteNotify : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_COMPLETE_NOTIFY;

    struct Payload {
        bool        success;    ///< true = fclose succeeded; false = write failed
        uint8_t     _pad[3];
        ota_fs_err_t last_error; ///< OTA_FS_OK on success, error code on failure
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaCompleteNotify(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
