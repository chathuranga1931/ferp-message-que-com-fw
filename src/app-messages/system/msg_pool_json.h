// msg_pool_json.h
//
// MsgPoolJson — pool status snapshot published by ModuleSysmon in response
// to MsgPoolGetJson.
//
// The JSON payload is too large to store inline in a fixed-size pool slab, so
// the slab holds only a pointer to a separately pool-allocated char buffer.
// The buffer is freed automatically via msg->cleanup when the last subscriber
// releases the message.
//
// Slab layout (sizeof(Payload) bytes, allocated from the 32-byte pool class):
//   bytes  0.. 3  uint32_t  json_len  — length of JSON string (excl. null)
//   bytes  4.. 7  (explicit padding)
//   bytes  8..15  void *    json_buf  — hsys_pool_alloc'd char[] for the JSON
//
// The inner json_buf is allocated from whichever pool class fits
// (json_len + 1) bytes, so it consumes only as much pool space as needed.
//
// JSON structure:
//   {"classes":[{"bs":<N>,"tot":<N>,"free":<N>,"peak":<N>},...]}
//
// Wire data field: the JSON object above is used directly as the codec
// "data" field — no extra wrapper.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgPoolJson : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_POOL_JSON;

    // Pool-slab struct — only the pointer and length live here.
    struct Payload {
        uint32_t json_len;   ///< Length of the JSON string (excluding null)
        uint8_t  _pad[4];    ///< Explicit padding for pointer alignment
        void    *json_buf;   ///< Pointer to a separately pool-allocated char[]
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}  // unused

    /**
     * Create and return a pool-backed MsgPoolJson.
     *
     * @param sender_id  Publishing module.
     * @param json       Null-terminated JSON string to embed.
     * @param json_len   Length of json (strlen result; null not counted).
     * @return           Ready-to-publish message, or nullptr on pool exhaustion.
     *
     * Ownership: the inner json_buf is freed automatically by msg->cleanup
     * when the message's ref_count reaches zero (last subscriber releases).
     */
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                               const char *json, uint32_t json_len);

    /**
     * Retrieve the JSON string embedded in a received message.
     *
     * @param msg      Message received in on_msg_received().
     * @param len_out  Optional: receives the JSON string length.
     * @return         Pointer to the null-terminated JSON string, or nullptr.
     *                 Valid only while the message's ref_count > 0.
     */
    static const char *get_json(const hsys_msg_t &msg, uint32_t *len_out = nullptr);

    // Codec interface
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};
