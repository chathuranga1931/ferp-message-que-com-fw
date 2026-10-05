// msg_cloud_print_status.h
//
// MsgCloudPrintStatus (0x0066) — ModuleCloudPrint → requester / all.
//
// DIRECT reply to MsgCloudPrintGetStatus.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "printer_types.h"
#include <stdint.h>

class MsgCloudPrintStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CLOUD_PRINT_STATUS;

    struct Payload {
        uint8_t  state;             ///< cloud_print_state_t
        bool     notify_subscribed; ///< notify topic is subscribed on MQTT
        uint8_t  _pad[2];
        int32_t  last_http;         ///< last HTTP status code (0 = none / transport error)
        uint32_t polls;             ///< queue polls since boot
        uint32_t printed;           ///< cloud jobs printed + ACKed since boot
        uint32_t failed;            ///< cloud jobs that failed to print since boot
        uint32_t rejected;          ///< cloud jobs REJECTED (bad data) since boot
        char     queue_id[48];
        char     notify_topic[64];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgCloudPrintStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
