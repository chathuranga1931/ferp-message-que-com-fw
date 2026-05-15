// msg_timer_start.h
//
// Typed message class for MSG_ID_TIMER_START.
//
// Sent by any module to ModuleTimer to request a new timer slot.
// ModuleTimer replies with a DIRECT MSG_ID_TIMER_START_RESPONSE to the
// module identified by source_module_id.
//
// When the timer fires, ModuleTimer sends a DIRECT MSG_ID_TIMER_ALARM
// to source_module_id.  For repetitive timers this repeats every
// duration_ms until a matching MSG_ID_TIMER_STOP is received.
//
// Publisher example:
//
//   MsgTimerStart::Payload p{};
//   p.source_module_id = id();
//   p.start_offset_ms  = 0;        // fire immediately after duration_ms
//   p.duration_ms      = 5000;     // 5 s
//   p.is_repetitive    = true;     // repeat forever
//   auto *msg = MsgTimerStart::create(id(), p);
//   publish(msg);
//
// On alarm:
//
//   case MsgTimerAlarm::ID:
//       auto p = MsgTimerAlarm::deserialize(msg);   // p.elapsed_ms etc.
//       break;

#ifndef MSG_TIMER_START_H
#define MSG_TIMER_START_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_TIMER_START

// ---------------------------------------------------------------------------
// MsgTimerStart
// ---------------------------------------------------------------------------

class MsgTimerStart : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TIMER_START;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        hsys_module_id_t  source_module_id;   ///< Module requesting the timer
        uint32_t          start_offset_ms;    ///< Additional delay before first fire
        uint32_t          duration_ms;        ///< Timer period / one-shot duration
        uint32_t          user_tag;           ///< Opaque tag echoed back in MsgTimerAlarm (0 = unused)
        bool              is_repetitive;      ///< true = repeat, false = one-shot
        bool              forced;             ///< true = stop any existing slot and restart
        uint8_t           _pad[2];            ///< Explicit alignment padding
    };

    // -----------------------------------------------------------------------
    // Descriptor — NOTIFICATION so ModuleTimer receives it from its task queue
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

    explicit MsgTimerStart(const Payload &payload) : m_payload(payload) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static helpers
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);

    /** Parse a flat JSON payload and return a ready-to-publish message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload m_payload;
};

#endif // MSG_TIMER_START_H
