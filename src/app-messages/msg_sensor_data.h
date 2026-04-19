// msg_sensor_data.h
//
// Typed message class for MSG_SENSOR_DATA.
//
// Encapsulates the sensor reading published by ModuleA every second.
// The Payload struct is the sole definition of the wire format — there is no
// separate module_a_sensor_data_t any more.
//
// Publisher (ModuleA):
//
//   MsgSensorData::Payload p{ .counter = m_counter, .temperature = 22.5f };
//   hsys_msg_t *msg = create_typed<MsgSensorData>(p);
//   publish(msg);
//
// Subscriber (ModuleB):
//
//   case MsgSensorData::ID: {
//       auto p = MsgSensorData::deserialize(msg);
//       printf("counter=%lu  temp=%.1f\n", p.counter, p.temperature);
//   }

#ifndef MSG_SENSOR_DATA_H
#define MSG_SENSOR_DATA_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"   // MSG_ID_SENSOR_DATA

// ---------------------------------------------------------------------------
// MsgSensorData
// ---------------------------------------------------------------------------

class MsgSensorData : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity  — assigned in app_msg_ids.h, not here
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_SENSOR_DATA;

    // -----------------------------------------------------------------------
    // Wire-format payload  (8 bytes, fits the 32-byte pool class)
    // -----------------------------------------------------------------------

    struct Payload {
        uint32_t counter;       ///< Monotonically increasing publish counter
        float    temperature;   ///< Simulated / real sensor reading (°C)
    };

    // -----------------------------------------------------------------------
    // Descriptor — referenced by app_msg_table.h
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

    explicit MsgSensorData(const Payload &payload) : m_payload(payload) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static factory — create a framework message and serialize payload into it
    // -----------------------------------------------------------------------

    /**
     * @brief  Create a ready-to-publish hsys_msg_t carrying a sensor payload.
     *
     * @param  sender_id  ID of the publishing module (injected by
     *                    HsysModule::create_typed() automatically).
     * @param  payload    The sensor data to serialize.
     * @return Pointer to a framework message, or nullptr on pool exhaustion.
     */
    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);

    // -----------------------------------------------------------------------
    // Static deserializer — called by the subscriber
    // -----------------------------------------------------------------------

    /**
     * @brief  Deserialize the payload from a received hsys_msg_t.
     *
     * Returns a default-constructed Payload if msg.payload is nullptr or
     * too small — the caller should check msg.payload_size before use if
     * defensive validation is needed.
     */
    static Payload deserialize(const hsys_msg_t &msg);

#ifdef FERP_SIMULATOR
    /** Simulator only — parse a flat JSON payload and return a ready-to-publish message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif

private:
    Payload m_payload;
};

#endif // MSG_SENSOR_DATA_H
