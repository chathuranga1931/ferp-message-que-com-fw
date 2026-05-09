// msg_dev_info_write.h
//
// MsgDevInfoWrite — write a value to a device info field by key.
//
// Only modules listed in the field's write-permission table will be
// accepted by ModuleDeviceInfo; others receive no response (logged as error).
//
// Publisher (ModuleCloud after onboarding):
//   MsgDevInfoWrite::Payload p{};
//   p.key  = DEV_INFO_KEY_DEVICE_UUID;
//   p.type = HSYS_TYPE_STRING;
//   strncpy(p.value.as_str, uuid_str, sizeof(p.value.as_str) - 1);
//   auto *msg = MsgDevInfoWrite::create(id(), p);
//   publish(msg);

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "hsys_type.h"
#include "app_msg_ids.h"

class MsgDevInfoWrite : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_DEV_INFO_WRITE;

    static constexpr uint16_t STR_MAX_LEN = 40;   // longest device info string field

    struct Payload {
        uint16_t       key;
        hsys_type_t    type;
        uint8_t        _pad[1];
        union {
            char     as_str[STR_MAX_LEN];
            uint32_t as_uint32;
            bool     as_bool;
        } value;
    };
    // sizeof(Payload) = 2 + 1 + 1 + 40 = 44 bytes → 64-byte pool block

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgDevInfoWrite(const Payload &p) : m_payload(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t   *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload       deserialize(const hsys_msg_t &msg);

    // Convenience factories
    static hsys_msg_t *create_str (hsys_module_id_t sender_id, uint16_t key, const char *str);
    static hsys_msg_t *create_u32 (hsys_module_id_t sender_id, uint16_t key, uint32_t val);
    static hsys_msg_t *create_bool(hsys_module_id_t sender_id, uint16_t key, bool val);

private:
    Payload m_payload{};
};
