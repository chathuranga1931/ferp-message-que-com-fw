// msg_sd_status.h
//
// MSG_ID_SD_STATUS — published by ModuleSD immediately after MsgSdReady.
//
// Carries detailed information about the mounted SD card so that any
// subscriber (UI, cloud reporter, diagnostics module, …) can display or
// act on it without needing to call app_sd directly.
//
// Payload fields
// ──────────────
//   status        — SdStatus enum  (MOUNTED | NOT_FOUND | ERROR)
//   card_type[32] — human-readable type string, e.g. "SDHC (sim)"
//   card_size_mb  — total capacity in megabytes
//   free_mb       — free (available) space in megabytes
//
// Wire layout (little-endian, 48 bytes):
//   [0]          uint8_t  status
//   [1..3]       pad
//   [4..11]      uint64_t card_size_mb
//   [12..19]     uint64_t free_mb
//   [20..51]     char[32] card_type   (NUL-terminated)

#ifndef MSG_SD_STATUS_H
#define MSG_SD_STATUS_H

#include <stdint.h>
#include <string.h>
#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgSdStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_SD_STATUS;

    // ------------------------------------------------------------------
    // Status values
    // ------------------------------------------------------------------
    enum SdStatus : uint8_t {
        SD_MOUNTED    = 0,   ///< Card present and mounted successfully
        SD_NOT_FOUND  = 1,   ///< No card detected / slot empty
        SD_ERROR      = 2,   ///< Card detected but init / mount failed
    };

    // ------------------------------------------------------------------
    // Payload  (POD — safe to memcpy on/off the wire)
    // ------------------------------------------------------------------
    struct Payload {
        SdStatus status       = SD_NOT_FOUND;
        uint8_t  _pad[3]      = {};
        uint64_t card_size_mb = 0;
        uint64_t free_mb      = 0;
        char     card_type[32]{};

        // Convenience
        bool is_mounted() const { return status == SD_MOUNTED; }

        const char *status_str() const {
            switch (status) {
                case SD_MOUNTED:   return "Mounted";
                case SD_NOT_FOUND: return "Not Found";
                case SD_ERROR:     return "Error";
                default:           return "Unknown";
            }
        }
    };

    // ------------------------------------------------------------------
    // Wire size = sizeof(Payload)
    // ------------------------------------------------------------------
    static constexpr size_t PAYLOAD_BYTES = sizeof(Payload);

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      PAYLOAD_BYTES,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // ------------------------------------------------------------------
    // Factory
    // ------------------------------------------------------------------
    static hsys_msg_t *create(hsys_module_id_t sender_id,
                              const Payload   &payload);

    // ------------------------------------------------------------------
    // IHsysMsg
    // ------------------------------------------------------------------
    explicit MsgSdStatus(const Payload &p) : _payload(p) {}

    hsys_msg_id_t msg_id()              const override { return ID; }
    void          serialize(hsys_msg_t *msg) const override;

    // ------------------------------------------------------------------
    // Deserializer
    // ------------------------------------------------------------------
    static Payload deserialize(const hsys_msg_t &msg);

#ifdef FERP_SIMULATOR
    /** Simulator — inject from JSON: {"status":0,"card_type":"SDHC","card_size_mb":1024,"free_mb":900} */
    static hsys_msg_t *from_json(const char *payload_json,
                                 hsys_module_id_t sender_id);
#endif

private:
    Payload _payload;
};

#endif // MSG_SD_STATUS_H
