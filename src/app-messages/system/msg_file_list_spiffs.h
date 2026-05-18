// msg_file_list_spiffs.h
//
// MsgFileListSpiffs — response published by ModuleSysmon containing a JSON
// listing of all files on the SPIFFS partition.
//
// The JSON string is variable-length, so it is stored in a separately
// pool-allocated inner buffer rather than inline in the slab.  The slab
// itself is small and fixed-size (fits the 32-byte pool class on ESP32).
//
// Slab layout (sizeof(Payload) bytes):
//   bytes  0.. 3  uint32_t  json_len   — byte length of JSON string (excl. NUL)
//   bytes  4.. 7  (explicit padding for pointer alignment)
//   bytes  8..15  void *    json_buf   — hsys_pool_alloc'd char[]
//
// JSON structure:
//   {"files":[{"name":"Configs/DeviceConfigs.json","size":512},...],"total_bytes":N,"free_bytes":N}

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

class MsgFileListSpiffs : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_FILE_LIST_SPIFFS;

    // Pool-slab struct — pointer + length only; inner buffer is separate.
    struct Payload {
        uint32_t json_len;   ///< Length of the JSON string (excluding NUL)
        uint8_t  _pad[4];    ///< Alignment pad for the following pointer
        void    *json_buf;   ///< Pointer to a separately pool-allocated char[]
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    hsys_msg_id_t msg_id()                        const override { return ID; }
    void          serialize(hsys_msg_t * /*msg*/) const override {}

    /**
     * Create a MsgFileListSpiffs with pool-backed JSON payload.
     *
     * @param sender_id  Publishing module.
     * @param json       NUL-terminated JSON string to embed.
     * @param json_len   strlen(json) — NUL not counted.
     * @return           Ready-to-publish message, or nullptr on pool exhaustion.
     */
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                              const char *json, uint32_t json_len);

    /**
     * Access the inner JSON string.
     *
     * @param msg     Message instance returned by create().
     * @param len_out If non-null, receives json_len.
     * @return        Pointer to the NUL-terminated JSON string, or nullptr.
     */
    static const char *get_json(const hsys_msg_t &msg, uint32_t *len_out = nullptr);

    /** JSON codec — response only; inbound decode not supported. */
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);

    /** JSON codec — copy the inner JSON string directly into the codec buffer. */
    static int32_t to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len);
};
