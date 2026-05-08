// msg_config_set.h
//
// MsgConfigSet — update one config field, identified by CFG_KEY_*.
//
// Binary slab layout (256-byte pool block) — identical to MsgConfigValue:
//   [0-1]   key        uint16_t LE  — CFG_KEY_* constant
//   [2]     type       uint8_t      — 0=STRING  1=UINT32  2=BOOL
//   [3]     (reserved)
//   [4-7]   data_size  uint32_t LE  — number of data bytes
//   [8+]    data                    — raw bytes
//
// JSON wire format from /api/messages:
//   {"key":4097,"type":0,"size":9,"data":[77,121,78,101,116,119,111,114,107]}
//
// C++ usage (internal modules still calling create_str / create_uint32):
//   auto *msg = MsgConfigSet::create_str(id(), CFG_KEY_WIFI_SSID, "MyNetwork");
//   publish(msg);

#ifndef MSG_CONFIG_SET_H
#define MSG_CONFIG_SET_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "hsys_type.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigSet : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_SET;

    // Slab constants — mirrors MsgConfigValue
    static constexpr uint16_t SLAB_SIZE   = 256U;
    static constexpr uint16_t HEADER_SIZE = 8U;
    static constexpr uint16_t MAX_DATA    = SLAB_SIZE - HEADER_SIZE;

    static constexpr uint16_t OFF_KEY     = 0U;
    static constexpr uint16_t OFF_TYPE    = 2U;
    static constexpr uint16_t OFF_SIZE    = 4U;
    static constexpr uint16_t OFF_DATA    = 8U;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      SLAB_SIZE,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgConfigSet() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}

    // -----------------------------------------------------------------------
    // Factory
    // -----------------------------------------------------------------------
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               uint16_t         key,
                               hsys_type_t      type,
                               const void      *data,
                               uint32_t         data_size);

    // Convenience helpers for C++ callers
    static hsys_msg_t *create_str   (hsys_module_id_t sender_id, uint16_t key, const char *value);
    static hsys_msg_t *create_uint32(hsys_module_id_t sender_id, uint16_t key, uint32_t   value);
    static hsys_msg_t *create_bool  (hsys_module_id_t sender_id, uint16_t key, bool        value);

    // -----------------------------------------------------------------------
    // Read helpers
    // -----------------------------------------------------------------------
    static uint16_t    get_key      (const hsys_msg_t &msg);
    static hsys_type_t get_type     (const hsys_msg_t &msg);
    static uint32_t    get_data_size(const hsys_msg_t &msg);
    static const void *get_data     (const hsys_msg_t &msg);

    // -----------------------------------------------------------------------
    // Codec
    // -----------------------------------------------------------------------
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json  (const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);
};

#endif // MSG_CONFIG_SET_H
