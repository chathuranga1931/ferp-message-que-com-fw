// msg_config_get_key.h
//
// MsgConfigGetKey — request a single config field by CFG_KEY_* identifier.
//
// Any module may publish this. ModuleConfig responds with MsgConfigValue
// sent DIRECT back to source_module_id.
//
// Wire layout (4-byte pool slab):
//   [0-1]  key               uint16_t  — CFG_KEY_* constant
//   [2-3]  source_module_id  uint16_t  — requester's module ID for direct reply

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigGetKey : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_GET_KEY;

    struct Payload {
        uint16_t         key;
        hsys_module_id_t source_module_id;
    };
    // sizeof(Payload) = 2 + 1 = 3 bytes → 4-byte pool block

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigGetKey(const Payload &p) : m_payload(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);

private:
    Payload m_payload{};
};
