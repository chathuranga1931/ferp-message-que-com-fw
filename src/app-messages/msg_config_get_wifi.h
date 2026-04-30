// msg_config_get_wifi.h
//
// Typed message class for MSG_ID_CONFIG_GET_WIFI.
//
// A notification broadcast by any module after receiving MsgConfigReady,
// requesting that ModuleConfig send back a DIRECT MsgConfigWifi response.
//
// Usage (requester):
//
//   MsgConfigGetWifi::Payload p{};
//   p.source_module_id = module_id();
//   hsys_msg_t *req = MsgConfigGetWifi::create(module_id(), p);
//   if (req) publish(req);
//
// Handler (ModuleConfig):
//
//   case MsgConfigGetWifi::ID: {
//       auto p = MsgConfigGetWifi::deserialize(msg);
//       // fill MsgConfigWifi payload and send DIRECT to p.source_module_id
//       break;
//   }

#ifndef MSG_CONFIG_GET_WIFI_H
#define MSG_CONFIG_GET_WIFI_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"    // MSG_ID_CONFIG_GET_WIFI

// ---------------------------------------------------------------------------
// MsgConfigGetWifi
// ---------------------------------------------------------------------------

class MsgConfigGetWifi : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_GET_WIFI;

    // -----------------------------------------------------------------------
    // Payload
    // -----------------------------------------------------------------------

    struct Payload {
        hsys_module_id_t source_module_id;  ///< Requester — response is sent DIRECT here
        uint8_t          _pad[3];
    };

    // -----------------------------------------------------------------------
    // Descriptor — NOTIFICATION: any module can subscribe / receive it
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

    explicit MsgConfigGetWifi(const Payload &payload) : m_payload(payload) {}

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

    static hsys_msg_t *mqtt_decode(const char *data_json, hsys_module_id_t sender_id);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_GET_WIFI_H
