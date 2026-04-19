// msg_printer_btn.h
//
// Typed message class for MSG_ID_PRINTER_BTN.
//
// Published by ModulePrintBtn when either print button is pressed and released.
//
// Payload:
//   button_id — which button: 1 = Print 1,  2 = Print 2
//   status    — BTN_SHORT_PRESS or BTN_LONG_PRESS
//
// Publisher (ModulePrintBtn):
//
//   MsgPrinterBtn::Payload p{};
//   p.button_id = 1;          // Print 1
//   p.status    = BTN_LONG_PRESS;
//   hsys_msg_t *msg = MsgPrinterBtn::create(id(), p);
//   publish(msg);
//
// Subscriber:
//
//   case MsgPrinterBtn::ID: {
//       auto p = MsgPrinterBtn::deserialize(msg);
//       if (p.button_id == 1 && p.status == BTN_SHORT_PRESS) { ... }
//       break;
//   }

#ifndef MSG_PRINTER_BTN_H
#define MSG_PRINTER_BTN_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"      // MSG_ID_PRINTER_BTN
#include "btn_press_type.h"   // btn_press_t

#include <stdint.h>

// ---------------------------------------------------------------------------
// MsgPrinterBtn
// ---------------------------------------------------------------------------

class MsgPrinterBtn : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_PRINTER_BTN;

    // -----------------------------------------------------------------------
    // Well-known button IDs
    // -----------------------------------------------------------------------

    static constexpr uint8_t BTN_ID_PRINT1 = 1;
    static constexpr uint8_t BTN_ID_PRINT2 = 2;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        uint8_t     button_id;   ///< 1 = Print 1,  2 = Print 2
        btn_press_t status;      ///< BTN_SHORT_PRESS or BTN_LONG_PRESS
        uint8_t     _pad[2];     ///< Alignment padding
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

    explicit MsgPrinterBtn(const Payload &payload) : m_payload(payload) {}

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

#endif // MSG_PRINTER_BTN_H
