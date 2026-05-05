// msg_wifi_event.cpp

#include "msg_wifi_event.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "MSG_WIFI"

void MsgWifiEvent::serialize(hsys_msg_t *msg) const
{
    if (!msg || !msg->payload) return;
    memcpy(msg->payload, &_p, sizeof(Payload));
}

hsys_msg_t *MsgWifiEvent::create(hsys_module_id_t sender_id, const Payload &p)
{
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg) {
        LOG_MSG_ERROR(true, "create: pool full");
        return nullptr;
    }
    MsgWifiEvent instance(p);
    instance.serialize(msg);
    return msg;
}

MsgWifiEvent::Payload MsgWifiEvent::deserialize(const hsys_msg_t &msg)
{
    Payload p{};
    if (msg.payload && msg.payload_size >= sizeof(Payload))
        memcpy(&p, msg.payload, sizeof(Payload));
    return p;
}

#include <ArduinoJson.h>
hsys_msg_t *MsgWifiEvent::from_json(const char *payload_json, hsys_module_id_t sender_id)
{
    JsonDocument doc;
    deserializeJson(doc, payload_json);

    Payload p{};
    const char *ev = doc["event"] | "";
    if      (strcmp(ev, "STA_CONNECTED")    == 0) p.event = WIFI_EVENT_STA_CONNECTED;
    else if (strcmp(ev, "STA_DISCONNECTED") == 0) p.event = WIFI_EVENT_STA_DISCONNECTED;
    else if (strcmp(ev, "STA_GOT_IP")       == 0) p.event = WIFI_EVENT_STA_GOT_IP;
    else if (strcmp(ev, "STA_RSSI_CHANGED") == 0) p.event = WIFI_EVENT_STA_RSSI_CHANGED;
    else if (strcmp(ev, "AP_START")         == 0) p.event = WIFI_EVENT_AP_START;
    else if (strcmp(ev, "AP_STOP")          == 0) p.event = WIFI_EVENT_AP_STOP;

    p.rssi = (int8_t)doc["rssi"].as<int>();
    strncpy(p.ip_address,  doc["ip"]   | "", sizeof(p.ip_address)  - 1);
    strncpy(p.ssid,        doc["ssid"] | "", sizeof(p.ssid)        - 1);
    strncpy(p.mac_address, doc["mac"]  | "", sizeof(p.mac_address) - 1);

    return MsgWifiEvent::create(sender_id, p);
}

int32_t MsgWifiEvent::to_json(const hsys_msg_t *msg, char *data_json, uint32_t buf_len)
{
    auto p = deserialize(*msg);
    static const char *ev_names[] = {
        "STA_CONNECTED", "STA_DISCONNECTED", "STA_GOT_IP",
        "STA_RSSI_CHANGED", "AP_START", "AP_STOP"
    };
    StaticJsonDocument<256> doc;
    doc["event"] = ((unsigned)p.event < 6) ? ev_names[p.event] : "UNKNOWN";
    doc["rssi"]  = p.rssi;
    doc["ip"]    = p.ip_address;
    doc["ssid"]  = p.ssid;
    doc["mac"]   = p.mac_address;
    size_t w = serializeJson(doc, data_json, buf_len);
    return (w > 0) ? 0 : -2;
}
