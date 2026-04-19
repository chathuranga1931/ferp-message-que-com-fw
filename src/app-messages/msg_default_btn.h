// msg_default_btn.h
//
// Typed message class for MSG_ID_DEFAULT_BTN.
//
// Published by ModuleDefaultBtn when the default (factory-reset / config) button
// is pressed and released.
//
// Payload:
//   status — BTN_SHORT_PRESS or BTN_LONG_PRESS
//
// Publisher (ModuleDefaultBtn):
//
//   MsgDefaultBtn::Payload p{};
//   p.status = BTN_SHORT_PRESS;
//   hsys_msg_t *msg = MsgDefaultBtn::create(id(), p);
//   publish(msg);
//
// Subscriber:
//
//   case MsgDefaultBtn::ID: {
//       auto p = MsgDefaultBtn::deserialize(msg);
//       if (p.status == BTN_SHORT_PRESS) { ... }
//       break;
//   }

#ifndef MSG_DEFAULT_BTN_H
#define MSG_DEFAULT_BTN_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"      // MSG_ID_DEFAULT_BTN
#include "btn_press_type.h"   // btn_press_t

// ---------------------------------------------------------------------------
// MsgDefaultBtn
// ---------------------------------------------------------------------------

class MsgDefaultBtn : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_DEFAULT_BTN;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        btn_press_t status;      ///< BTN_SHORT_PRESS or BTN_LONG_PRESS
        uint8_t     _pad[3];     ///< Alignment padding
    };
    // sizeof(Payload) = 4 bytes → fits in the 4-byte pool class

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

    explicit MsgDefaultBtn(const Payload &payload) : m_payload(payload) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static factory
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);

    // -----------------------------------------------------------------------
    // Static deserializer
    // -----------------------------------------------------------------------

    static Payload deserialize(const hsys_msg_t &msg);

#ifdef FERP_SIMULATOR
    /** Simulator only — parse a flat JSON payload and return a ready-to-publish message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif

private:
    Payload m_payload;
};

#endif // MSG_DEFAULT_BTN_H
