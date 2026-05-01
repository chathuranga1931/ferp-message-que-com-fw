// msg_timer_stop.h
//
// Typed message class for MSG_ID_TIMER_STOP.
//
// Sent by any module to ModuleTimer to cancel a previously started timer.
// ModuleTimer replies with a DIRECT MSG_ID_TIMER_STOP_RESPONSE.
// If no active slot exists for source_module_id the response carries
// TIMER_RESULT_ERR_NOT_FOUND.
//
// Publisher example:
//
//   MsgTimerStop::Payload p{};
//   p.source_module_id = id();
//   auto *msg = MsgTimerStop::create(id(), p);
//   publish(msg);

#ifndef MSG_TIMER_STOP_H
#define MSG_TIMER_STOP_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_TIMER_STOP

// ---------------------------------------------------------------------------
// MsgTimerStop
// ---------------------------------------------------------------------------

class MsgTimerStop : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TIMER_STOP;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        hsys_module_id_t  source_module_id;   ///< Module whose timer to cancel
        uint8_t           _pad[3];            ///< Alignment padding
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

    explicit MsgTimerStop(const Payload &payload) : m_payload(payload) {}

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

#ifdef FERP_SIMULATOR
    /** Simulator only — parse a flat JSON payload and return a ready-to-publish message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif

private:
    Payload m_payload;
};

#endif // MSG_TIMER_STOP_H
