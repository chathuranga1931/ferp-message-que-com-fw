// msg_dev_info_value.h
//
// MsgDevInfoValue — direct response from ModuleDeviceInfo to a read requester.
//
// Contains the key, type, validity flag, and the actual value.
// Sent DIRECT to the requesting module (not broadcast).
//
// Receiver:
//   case MsgDevInfoValue::ID: {
//       auto p = MsgDevInfoValue::deserialize(msg);
//       if (!p.is_valid) { /* not yet populated */ break; }
//       if (p.key == DEV_INFO_KEY_DEVICE_UUID) {
//           strncpy(m_uuid, p.value.as_str, sizeof(m_uuid) - 1);
//       }
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "hsys_type.h"
#include "app_msg_ids.h"

class MsgDevInfoValue : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_DEV_INFO_VALUE;

    static constexpr uint16_t STR_MAX_LEN = 40;

    struct Payload {
        uint16_t       key;
        hsys_type_t    type;
        bool           is_valid;
        uint8_t        _pad[1];
        union {
            char     as_str[STR_MAX_LEN];
            uint32_t as_uint32;
            bool     as_bool;
        } value;
    };
    // sizeof(Payload) = 2 + 1 + 1 + 1 + 1 + 40 = 46 bytes → 64-byte pool block

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgDevInfoValue(const Payload &p) : m_payload(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t   *create(hsys_module_id_t sender_id,
                                 hsys_module_id_t receiver_id,
                                 const Payload   &payload);
    static Payload       deserialize(const hsys_msg_t &msg);

private:
    Payload m_payload{};
};
