// msg_timer_start_response.h
//
// Typed message class for MSG_ID_TIMER_START_RESPONSE.
//
// Sent DIRECT by ModuleTimer to the module that issued MSG_ID_TIMER_START.
// The receiver inspects result to confirm its slot was allocated.
//
// Receiver example:
//
//   case MsgTimerStartResponse::ID: {
//       auto p = MsgTimerStartResponse::deserialize(msg);
//       if (p.result != TIMER_RESULT_OK) {
//           log_error("timer start failed: %d", (int)p.result);
//       }
//       break;
//   }

#ifndef MSG_TIMER_START_RESPONSE_H
#define MSG_TIMER_START_RESPONSE_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_TIMER_START_RESPONSE
#include "timer_types.h"    // timer_result_t

// ---------------------------------------------------------------------------
// MsgTimerStartResponse
// ---------------------------------------------------------------------------

class MsgTimerStartResponse : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_TIMER_START_RESPONSE;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        hsys_module_id_t  source_module_id;   ///< Module the response is for
        timer_result_t    result;             ///< Outcome of the start request
        uint8_t           _pad[2];            ///< Alignment padding
    };

    // -----------------------------------------------------------------------
    // Descriptor — DIRECT: point-to-point back to the requester
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

    explicit MsgTimerStartResponse(const Payload &payload) : m_payload(payload) {}

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

#endif // MSG_TIMER_START_RESPONSE_H
