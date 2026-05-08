// msg_config_value.h
//
// MsgConfigValue — single-field config value response from ModuleConfig.
//
// Sent DIRECT to the module that issued MsgConfigGetKey.
//
// Binary slab layout (256-byte pool block):
//   [0-1]   key        uint16_t LE  — CFG_KEY_* that was requested
//   [2]     type       uint8_t      — 0=STRING  1=UINT32  2=BOOL
//   [3]     (reserved/pad)
//   [4-7]   data_size  uint32_t LE  — number of data bytes that follow
//   [8+]    data                    — raw bytes (string: no NUL stored; uint32/bool: LE)
//
// Maximum data: 256 - 8 = 248 bytes (fits the longest config string: 128 B)
//
// JSON wire encoding (for /api/messages response):
//   {"key":4097,"type":0,"size":9,"data":[77,121,78,101,116,119,111,114,107]}
//
// type constants (same as HSYS_TYPE_*):
//   0 = STRING   (HSYS_TYPE_STRING)
//   1 = UINT32   (HSYS_TYPE_UINT32)
//   2 = BOOL     (HSYS_TYPE_BOOL)

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "hsys_type.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgConfigValue : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_VALUE;

    // Slab size — must fit all slab fields + max data (128-byte string)
    static constexpr uint16_t SLAB_SIZE    = 256U;
    static constexpr uint16_t HEADER_SIZE  = 8U;
    static constexpr uint16_t MAX_DATA     = SLAB_SIZE - HEADER_SIZE;  // 248 bytes

    // Byte offsets within the pool slab
    static constexpr uint16_t OFF_KEY      = 0U;   // uint16_t
    static constexpr uint16_t OFF_TYPE     = 2U;   // uint8_t
    static constexpr uint16_t OFF_PAD      = 3U;   // uint8_t  (reserved)
    static constexpr uint16_t OFF_SIZE     = 4U;   // uint32_t
    static constexpr uint16_t OFF_DATA     = 8U;   // data bytes

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      SLAB_SIZE,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    MsgConfigValue() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}   // not used — create() writes directly

    // -----------------------------------------------------------------------
    // Factory — build from raw components
    // -----------------------------------------------------------------------
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               hsys_module_id_t receiver_id,
                               uint16_t         key,
                               hsys_type_t      type,
                               const void      *data,
                               uint32_t         data_size);

    // -----------------------------------------------------------------------
    // Read helpers — parse out of the received slab
    // -----------------------------------------------------------------------
    static uint16_t    get_key      (const hsys_msg_t &msg);
    static hsys_type_t get_type     (const hsys_msg_t &msg);
    static uint32_t    get_data_size(const hsys_msg_t &msg);
    static const void *get_data     (const hsys_msg_t &msg);

    // -----------------------------------------------------------------------
    // Codec — used by app_msg_codec / ModuleWebServer /api/messages response
    // -----------------------------------------------------------------------
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json_out, uint32_t buf_len);
};
