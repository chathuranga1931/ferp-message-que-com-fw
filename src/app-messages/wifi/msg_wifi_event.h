// msg_wifi_event.h
//
// Published by ModuleWifi (or the simulator bridge) on every WiFi state change.
//
// Subscriber:
//
//   case MsgWifiEvent::ID: {
//       auto p = MsgWifiEvent::deserialize(msg);
//       if (p.event == WIFI_EVENT_STA_GOT_IP) { ... }
//       break;
//   }

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Event enum
// ---------------------------------------------------------------------------

typedef enum : uint8_t {
    WIFI_EVENT_STA_CONNECTED    = 0,
    WIFI_EVENT_STA_DISCONNECTED = 1,
    WIFI_EVENT_STA_GOT_IP       = 2,
    WIFI_EVENT_STA_RSSI_CHANGED = 3,
    WIFI_EVENT_AP_START         = 4,
    WIFI_EVENT_AP_STOP          = 5,
} wifi_event_id_t;

// ---------------------------------------------------------------------------
// MsgWifiEvent
// ---------------------------------------------------------------------------

class MsgWifiEvent : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_WIFI_EVENT;

    struct Payload {
        wifi_event_id_t event;
        int8_t          rssi;
        uint8_t         _pad[2];
        char            ip_address[16];   ///< dot-decimal, valid on GOT_IP only
        char            ssid[50];
        char            mac_address[18];  ///< "XX:XX:XX:XX:XX:XX", valid on GOT_IP
    };

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_NOTIFICATION,
                      sizeof(Payload),
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    explicit MsgWifiEvent(const Payload &p) : _p(p) {}

    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *msg) const override;

    static hsys_msg_t *create(hsys_module_id_t sender_id, const Payload &p);
    static Payload     deserialize(const hsys_msg_t &msg);

    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
    static int32_t     to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len);

private:
    Payload _p;
};
