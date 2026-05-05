// msg_tick_1000ms.h
//
// Typed message class for MSG_TICK_1000MS.
//
// A notification-only heartbeat with no payload, fired every 1 000 ms by
// the Ticker module.
//
// Publisher (Ticker):
//
//   hsys_msg_t *msg = create_typed<MsgTick1000ms>(MsgTick1000ms::Payload{});
//   publish(msg);
//
// Subscriber (any module that needs a 1-second heartbeat):
//
//   case MsgTick1000ms::ID:
//       // no payload to deserialize — just react to the tick
//       break;

#ifndef MSG_TICK_1000MS_H
#define MSG_TICK_1000MS_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"   // MSG_ID_TICK_1000MS

// ---------------------------------------------------------------------------
// MsgTick1000ms
// ---------------------------------------------------------------------------

class MsgTick1000ms : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity  — assigned in app_msg_ids.h, not here
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_TICK_1000MS;

    // -----------------------------------------------------------------------
    // Payload — empty struct; exists for API uniformity with create_typed<T>
    // -----------------------------------------------------------------------

    struct Payload {};

    // -----------------------------------------------------------------------
    // Descriptor — referenced by app_msg_table.h
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,               // no payload bytes
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    explicit MsgTick1000ms(const Payload & /*unused*/) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t * /*msg*/) const override {}  // nothing to write

    // -----------------------------------------------------------------------
    // Static factory
    // -----------------------------------------------------------------------

    /**
     * @brief  Create a ready-to-publish hsys_msg_t for the 1 s tick.
     *
     * @param  sender_id  Injected automatically by HsysModule::create_typed().
     * @return Pointer to a framework message, or nullptr on pool exhaustion.
     */
    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload & /*unused*/);

    // -----------------------------------------------------------------------
    // Deserializer (no-op — returns empty Payload)
    // -----------------------------------------------------------------------

    static Payload deserialize(const hsys_msg_t & /*msg*/) { return {}; }

    /** No payload; just creates and returns the message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};

#endif // MSG_TICK_1000MS_H
