// msg_config_get.h
//
// Typed message class for MSG_ID_CONFIG_GET.
//
// A zero-payload notification that tells ModuleConfig to re-publish the
// current config as a fresh MSG_ID_CONFIG_READY message.
//
// Typical senders:
//   - ModuleMqtt      — on receipt of "ferp/config/get" topic
//   - ModuleWebServer — on GET /config REST request
//   - ModuleSimBridge — on "SIM_CFG_GET" TCP command from Python UI
//   - Any module      — after reconnect, to refresh its cached config values
//
// Publisher:
//
//   auto *msg = MsgConfigGet::create(module_id());
//   publish(msg);
//
// Receiver (ModuleConfig only):
//
//   case MsgConfigGet::ID:
//       publish_config_ready();   // re-publishes MsgConfigReady
//       break;

#ifndef MSG_CONFIG_GET_H
#define MSG_CONFIG_GET_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_CONFIG_GET

// ---------------------------------------------------------------------------
// MsgConfigGet
// ---------------------------------------------------------------------------

class MsgConfigGet : public IHsysMsg
{
public:
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_GET;

    // -----------------------------------------------------------------------
    // Descriptor — zero payload_size, uses no pool buffer
    // -----------------------------------------------------------------------

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      0,               // no payload buffer needed
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    MsgConfigGet() = default;

    // -----------------------------------------------------------------------
    // IHsysMsg interface
    // -----------------------------------------------------------------------

    hsys_msg_id_t msg_id() const override { return ID; }

    void serialize(hsys_msg_t * /*msg*/) const override {}   // nothing to copy

    // -----------------------------------------------------------------------
    // Static factory
    // -----------------------------------------------------------------------

    static hsys_msg_t *create(hsys_module_id_t sender_id);

    /** No payload; just creates and returns the message. */
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);
};

#endif // MSG_CONFIG_GET_H
