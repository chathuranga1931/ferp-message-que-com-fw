// msg_time_status.h
//
// Typed message class for MSG_ID_TIME_STATUS.
//
// Published by ModuleTimeMgr whenever the time source changes or a new
// synchronization is completed.  All subscribers receive the current
// best available time and a flag indicating whether it is considered valid.
//
// Source priority (lowest → highest):
//   TIME_SOURCE_NONE   — no time available yet
//   TIME_SOURCE_BACKUP — SPIFFS backup file (±5 min accuracy)
//   TIME_SOURCE_RTC    — DS1307 hardware RTC (battery-backed)
//   TIME_SOURCE_NTP    — NTP (highest accuracy, internet required)

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Time source enumeration
// ---------------------------------------------------------------------------

typedef enum : uint8_t {
    TIME_SOURCE_NONE   = 0,   ///< No time source available
    TIME_SOURCE_BACKUP = 1,   ///< SPIFFS backup file
    TIME_SOURCE_RTC    = 2,   ///< DS1307 hardware RTC
    TIME_SOURCE_NTP    = 3,   ///< NTP synchronised
} time_source_t;

// ---------------------------------------------------------------------------
// MsgTimeStatus
// ---------------------------------------------------------------------------

class MsgTimeStatus : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TIME_STATUS;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        time_t   epoch;     ///< Unix timestamp (0 = no valid time)
        uint8_t  source;    ///< time_source_t cast to uint8_t
        bool     valid;     ///< false = no reliable source yet
        uint8_t  _pad[2];   ///< Explicit alignment padding
    };

    // -----------------------------------------------------------------------
    // Descriptor
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    explicit MsgTimeStatus(const Payload &p) : _p(p) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static helpers
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
