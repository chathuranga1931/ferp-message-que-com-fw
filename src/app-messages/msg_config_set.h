// msg_config_set.h
//
// Typed message class for MSG_ID_CONFIG_SET.
//
// Sent to ModuleConfig by any module that needs to update one config field.
// Sources:  ModuleMqtt (MQTT command), ModuleWebServer (REST API),
//           ModuleSimBridge (simulator TCP command from Python UI).
//
// The payload is a fixed struct carrying:
//   key    — field name (matches an entry in app_config_fields.h)
//   type   — data type (string / uint32 / bool)
//   value  — union large enough for the biggest field (128-byte string)
//
// Fixed size is intentional: config updates are rare (a few per day), so
// the marginal pool saving of a variable-size format does not justify the
// extra serialiser complexity.  Pool cost: one 256-byte block per update.
//
// Publisher example (ModuleMqtt sets wifi_ssid):
//
//   MsgConfigSet::Payload p{};
//   strncpy(p.key, "wifi_ssid", sizeof(p.key));
//   p.type = HSYS_TYPE_STRING;
//   strncpy(p.value.as_str, "MyNetwork", sizeof(p.value.as_str));
//   auto *msg = MsgConfigSet::create(module_id(), p);
//   publish(msg);
//
// Receiver (ModuleConfig):
//
//   case MsgConfigSet::ID: {
//       auto p = MsgConfigSet::deserialize(msg);
//       apply_field(p.key, p.type, p.value);
//   }

#ifndef MSG_CONFIG_SET_H
#define MSG_CONFIG_SET_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "hsys_type.h"      // hsys_type_t  (HSYS_TYPE_STRING / UINT32 / BOOL)
#include "app_msg_ids.h"    // MSG_ID_CONFIG_SET

// ---------------------------------------------------------------------------
// MsgConfigSet
// ---------------------------------------------------------------------------

class MsgConfigSet : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_SET;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    static constexpr uint16_t KEY_MAX_LEN  = 16;
    static constexpr uint16_t STR_MAX_LEN  = 128;  // longest config field

    struct Payload {
        char         key[KEY_MAX_LEN];   ///< Config field name  e.g. "wifi_ssid"
        hsys_type_t  type;               ///< HSYS_TYPE_STRING / UINT32 / BOOL
        uint8_t        _pad[3];            ///< Explicit alignment padding
        union {
            bool     as_bool;
            uint32_t as_uint32;
            char     as_str[STR_MAX_LEN];
        } value;
    };
    // sizeof(Payload) = 16 + 1 + 3 + 128 = 148 bytes → 256-byte pool block

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

    explicit MsgConfigSet(const Payload &payload) : m_payload(payload) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static factory — convenience helpers per type
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);

    /** Convenience: create a string-valued set request. */
    static hsys_msg_t *create_str(hsys_module_id_t sender_id,
                                   const char      *key,
                                   const char      *value);

    /** Convenience: create a uint32-valued set request. */
    static hsys_msg_t *create_uint32(hsys_module_id_t sender_id,
                                      const char      *key,
                                      uint32_t         value);

    /** Convenience: create a bool-valued set request. */
    static hsys_msg_t *create_bool(hsys_module_id_t sender_id,
                                    const char      *key,
                                    bool             value);

    // -----------------------------------------------------------------------
    // Static deserializer
    // -----------------------------------------------------------------------

    static Payload deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *mqtt_decode(const char *data_json, hsys_module_id_t sender_id);

private:
    Payload m_payload;
};

#endif // MSG_CONFIG_SET_H
