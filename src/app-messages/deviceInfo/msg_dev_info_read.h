// msg_dev_info_read.h
//
// MsgDevInfoRead — request a single device info field by key.
//
// Any module may send this to ModuleDeviceInfo.  The module responds
// with MsgDevInfoValue sent directly back to the requester.
//
// Publisher:
//   MsgDevInfoRead::Payload p{ .key = DEV_INFO_KEY_DEVICE_UUID,
//                              .source_module_id = id() };
//   auto *msg = MsgDevInfoRead::create(id(), p);
//   publish(msg);

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgDevInfoRead : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_DEV_INFO_READ;

    struct Payload {
        uint16_t           key;
        hsys_module_id_t   source_module_id;
    };
    // sizeof(Payload) = 2 + 1 = 3 bytes → 4-byte pool block

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgDevInfoRead(const Payload &p) : m_payload(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t    *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload        deserialize(const hsys_msg_t &msg);

    static hsys_msg_t    *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t        to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);

private:
    Payload m_payload{};
};
