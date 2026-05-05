// msg_persist_log.h
//
// MsgPersistLog — published by any module (or HsysModule::log_persistent())
// to request that a text string be written to the SD-card persistent log.
//
// The message is a broadcast NOTIFICATION consumed exclusively by ModulePLog
// (module_plog). All other modules may publish it freely.
//
// Usage:
//   log_persistent("nozzle %u triggered", nozzle_idx);  // from any HsysModule
//
//   // Or manually:
//   MsgPersistLog::Payload p{};
//   snprintf(p.text, sizeof(p.text), "custom message");
//   publish(create_typed<MsgPersistLog>(p));

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

// ---------------------------------------------------------------------------
// MsgPersistLog
// ---------------------------------------------------------------------------

class MsgPersistLog : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_PERSIST_LOG;

    struct Payload {
        char text[192];  ///< Null-terminated log string (truncated if longer)
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgPersistLog(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id()                 const override { return ID; }
    void          serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

private:
    Payload _p;
};
