// msg_config_updated.h
//
// MsgConfigUpdated — published by ModuleConfig after any field is written
// via MsgConfigSet.
//
// Payload: one uint16_t — the CFG_KEY_* that was changed.
// Subscribers read the key and decide whether it is relevant to them.
// If so, they call app_config_get() directly to obtain the new value.
//
// Binary slab layout (4-byte pool block):
//   [0-1]  key  uint16_t LE  — CFG_KEY_* constant
//
// Publisher (ModuleConfig):
//   hsys_msg_t *msg = MsgConfigUpdated::create(id(), CFG_KEY_DT_LOG_RATE);
//   publish(msg);
//
// Subscriber:
//   case MsgConfigUpdated::ID: {
//       uint16_t key = MsgConfigUpdated::get_key(msg);
//       if (key == CFG_KEY_DT_LOG_RATE) { /* re-read config */ }
//       break;
//   }

#ifndef MSG_CONFIG_UPDATED_H
#define MSG_CONFIG_UPDATED_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigUpdated : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_UPDATED;

    static constexpr uint16_t SLAB_SIZE = 4U;
    static constexpr uint16_t OFF_KEY   = 0U;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      SLAB_SIZE,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgConfigUpdated() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}

    /** Create a notification carrying the updated CFG_KEY_*. */
    static hsys_msg_t *create(hsys_module_id_t sender_id, uint16_t key);

    /** Extract the key from a received message. */
    static uint16_t get_key(const hsys_msg_t &msg);

    // JSON bridge (used by the web message layer)
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json  (const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);
};

#endif // MSG_CONFIG_UPDATED_H
