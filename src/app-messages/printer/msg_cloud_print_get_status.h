// msg_cloud_print_get_status.h
//
// MsgCloudPrintGetStatus (0x0065) - Any -> ModuleCloudPrint: request MsgCloudPrintStatus.
//
// DIRECT to ModuleCloudPrint; the status is sent DIRECT back to the sender.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgCloudPrintGetStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CLOUD_PRINT_GET_STATUS;

    struct Payload {
        uint32_t reserved;   ///< keep 0
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgCloudPrintGetStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p = {});
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
