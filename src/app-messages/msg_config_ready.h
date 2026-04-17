// msg_config_ready.h
//
// Typed message class for MSG_ID_CONFIG_READY.
//
// Published by ModuleConfig in two situations:
//   1. At boot, after the config file has been read and defaults merged.
//   2. After every successful MSG_ID_CONFIG_SET update.
//
// The payload carries a pointer to ModuleConfig's internal static
// app_config_t instance.  Subscribers MUST only read the pointer inside
// their on_msg_received() callback — do NOT store it.  The instance is
// valid for the lifetime of the firmware but fields may be updated by a
// future CONFIG_SET; storing a stale pointer is safe but the values may
// be out of date.
//
// Publisher (ModuleConfig):
//
//   MsgConfigReady::Payload p{ .config = &m_config };
//   auto *msg = create_typed<MsgConfigReady>(p);
//   publish(msg);
//
// Subscriber (e.g. ModuleWifi):
//
//   case MsgConfigReady::ID: {
//       auto p = MsgConfigReady::deserialize(msg);
//       if (p.config) {
//           strncpy(m_ssid, p.config->wifi_ssid, sizeof(m_ssid));
//       }
//   }

#ifndef MSG_CONFIG_READY_H
#define MSG_CONFIG_READY_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"        // MSG_ID_CONFIG_READY
#include "app_config.h"         // app_config_t

// ---------------------------------------------------------------------------
// MsgConfigReady
// ---------------------------------------------------------------------------

class MsgConfigReady : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_READY;

    // -----------------------------------------------------------------------
    // Payload — pointer only (8 bytes); the config struct lives in
    // ModuleConfig's static storage, not in the pool buffer.
    // -----------------------------------------------------------------------

    struct Payload {
        const app_config_t *config;   ///< Pointer to the live config instance
    };

    // -----------------------------------------------------------------------
    // Descriptor — small pool block (32 bytes) because payload is a pointer
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

    explicit MsgConfigReady(const Payload &payload) : m_payload(payload) {}

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t *msg) const override;

    // -----------------------------------------------------------------------
    // Static factory
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t      sender_id,
                               const app_config_t   *config);

    // -----------------------------------------------------------------------
    // Static deserializer
    // -----------------------------------------------------------------------

    static Payload deserialize(const hsys_msg_t &msg);

private:
    Payload m_payload;
};

#endif // MSG_CONFIG_READY_H
