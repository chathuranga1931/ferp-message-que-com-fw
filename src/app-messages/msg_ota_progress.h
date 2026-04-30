// msg_ota_progress.h
//
// NOTIFICATION published by the OTA source during the ACTIVE state.
// ModuleOta subscribes only to reset its inactivity timer.
// Other subscribers (ModuleLeds, ModuleBuzzer) use it to show progress.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgOtaProgress : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_OTA_PROGRESS;

    struct Payload {
        uint8_t  target_idx;    ///< Same index as in MsgOtaStartRequest
        uint8_t  percent;       ///< 0–100 (authoritative progress indicator)
        uint8_t  _pad[2];
        uint32_t bytes_written; ///< Cumulative bytes written so far
        uint32_t total_bytes;   ///< Total firmware size (0 if unknown)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgOtaProgress(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
