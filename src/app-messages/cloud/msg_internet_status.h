// msg_internet_status.h
//
// Published by ModuleInternet when internet reachability changes.
//
// Subscriber:
//
//   case MsgInternetStatus::ID: {
//       auto p = MsgInternetStatus::deserialize(msg);
//       if (p.connected) { ... }
//       break;
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgInternetStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_INTERNET_STATUS;

    struct Payload {
        bool    connected;
        uint8_t _pad[3];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgInternetStatus(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);
    static hsys_msg_t *mqtt_decode(const char *data_json, hsys_module_id_t sender_id);
    static int32_t      mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
