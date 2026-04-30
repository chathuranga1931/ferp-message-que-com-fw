// msg_config_wifi.h
//
// Typed message class for MSG_ID_CONFIG_WIFI.
//
// Sent DIRECT by ModuleConfig in response to MsgConfigGetWifi.
// The receiver (ModuleWifi / any requester) caches the credentials.
//
// Usage (ModuleConfig sender):
//
//   MsgConfigWifi::Payload p{};
//   strncpy(p.ssid,     cfg->wifi_ssid,     sizeof(p.ssid)     - 1);
//   strncpy(p.password, cfg->wifi_password, sizeof(p.password) - 1);
//   hsys_msg_t *out = MsgConfigWifi::create(MODULE_CONFIG_ID, p);
//   if (out) { out->receiver_id = requester_id; send(out); }
//
// Usage (receiver):
//
//   case MsgConfigWifi::ID: {
//       auto p = MsgConfigWifi::deserialize(msg);
//       strncpy(_ssid, p.ssid, sizeof(_ssid) - 1);
//       break;
//   }

#ifndef MSG_CONFIG_WIFI_H
#define MSG_CONFIG_WIFI_H

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"

class MsgConfigWifi : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_CONFIG_WIFI;

    struct Payload {
        char ssid[64];
        char password[64];
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgConfigWifi(const Payload &payload) : m_payload(payload) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &payload);
    static Payload     deserialize(const hsys_msg_t &msg);
    static int32_t     mqtt_encode(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload m_payload{};
};

#endif // MSG_CONFIG_WIFI_H
