// msg_timer_alarm.h
//
// Typed message class for MSG_ID_TIMER_ALARM.
//
// Sent DIRECT by ModuleTimer to the module whose timer just fired.
// For repetitive timers this is sent once every duration_ms until
// a matching MSG_ID_TIMER_STOP is received.
//
// Receiver example:
//
//   case MsgTimerAlarm::ID: {
//       auto p = MsgTimerAlarm::deserialize(msg);
//       // p.source_module_id == id() — sanity check
//       // p.elapsed_ms — ms since timer was started
//       handle_timeout();
//       break;
//   }

#ifndef MSG_TIMER_ALARM_H
#define MSG_TIMER_ALARM_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_TIMER_ALARM

// ---------------------------------------------------------------------------
// MsgTimerAlarm
// ---------------------------------------------------------------------------

class MsgTimerAlarm : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TIMER_ALARM;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        hsys_module_id_t  source_module_id;   ///< Module the alarm is for
        uint32_t          elapsed_ms;         ///< ms since timer was started
    };

    // -----------------------------------------------------------------------
    // Descriptor — DIRECT: delivered only to the registered module
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    explicit MsgTimerAlarm(const Payload &payload) : m_payload(payload) {}

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

private:
    Payload m_payload;
};

#endif // MSG_TIMER_ALARM_H
