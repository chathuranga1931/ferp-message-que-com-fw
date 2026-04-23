/**
 * @file module_sim_bridge.cpp
 * @brief Simulator bridge — subscribes to HSYS messages and forwards them to
 *        the Python UI via mac_driver_send_json().
 *
 * All TCP I/O (socket, accept loop, read loop, button injection) lives in
 * mac_driver.cpp.  This module's only job is message serialisation.
 *
 * To add a new message type:
 *   1. subscribe(MsgXxx::ID) in init()
 *   2. Add a case in on_msg_received() that calls _send_json()
 */

#include "module_sim_bridge.h"
#include "mac_driver.h"
#include "app_msg_ids.h"
#include "hsys_pool.h"
#include "hsys_msg.h"
#include "pal_time.h"
#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_cloud_status.h"

#include <stdio.h>
#include <string.h>

// ─── Singleton ───────────────────────────────────────────────────────────────

static ModuleSimBridge s_instance;
ModuleSimBridge *ModuleSimBridge::instance() { return &s_instance; }

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void ModuleSimBridge::init()
{
    // subscribe(MSG_ID_TICK_1000MS);
    subscribe(MSG_ID_SPIFFS_READY);
    subscribe(MSG_ID_WIFI_EVENT);
    subscribe(MSG_ID_INTERNET_STATUS);
    subscribe(MSG_ID_CLOUD_STATUS);

    log("init");
}

// ─── Message handler ─────────────────────────────────────────────────────────

void ModuleSimBridge::on_msg_received(const hsys_msg_t &msg)
{
    char data[256];

    switch (msg.msg_id)
    {
        case MSG_ID_TICK_1000MS:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_TICK_1000MS", data);
            ++_tick_count;
            if (_tick_count % POOL_REPORT_INTERVAL_TICKS == 0) {
                _send_pool_status();
            }
            break;

        case MSG_ID_SPIFFS_READY:
            _spiffs_ready = true;
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_SPIFFS_READY", data);
            break;

        // ── Add cases here as each sprint adds message types ──────────────

        case MSG_ID_WIFI_EVENT: {
            auto p = MsgWifiEvent::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\",\"rssi\":%d,\"ip\":\"%s\","
                     "\"ssid\":\"%s\",\"mac\":\"%s\"}",
                     _wifi_event_str(p.event), (int)p.rssi,
                     p.ip_address, p.ssid, p.mac_address);
            _send_json("MSG_WIFI_EVENT", data);
            break;
        }

        case MSG_ID_INTERNET_STATUS: {
            auto p = MsgInternetStatus::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"connected\":%s}", p.connected ? "true" : "false");
            _send_json("MSG_INTERNET_STATUS", data);
            break;
        }

        case MSG_ID_CLOUD_STATUS: {
            auto p = MsgCloudStatus::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\",\"nozzle_idx\":%u}",
                     _cloud_status_str(p.event), (unsigned)p.nozzle_idx);
            _send_json("MSG_CLOUD_STATUS", data);
            break;
        }

        default:
            break;
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static const char *_wifi_event_str(wifi_event_id_t e)
{
    switch (e) {
        case WIFI_EVENT_STA_CONNECTED:    return "STA_CONNECTED";
        case WIFI_EVENT_STA_DISCONNECTED: return "STA_DISCONNECTED";
        case WIFI_EVENT_STA_GOT_IP:       return "GOT_IP";
        case WIFI_EVENT_STA_RSSI_CHANGED: return "STA_RSSI_CHANGED";
        case WIFI_EVENT_AP_START:         return "AP_START";
        case WIFI_EVENT_AP_STOP:          return "AP_STOP";
        default:                          return "UNKNOWN";
    }
}

static const char *_cloud_status_str(cloud_status_event_t e)
{
    switch (e) {
        case CLOUD_STATUS_REGISTERED:      return "REGISTERED";
        case CLOUD_STATUS_REGISTER_FAILED: return "REGISTER_FAILED";
        case CLOUD_STATUS_PUMPED_SUCCESS:  return "PUMPED_SUCCESS";
        case CLOUD_STATUS_PUMPED_FAILED:   return "PUMPED_FAILED";
        case CLOUD_STATUS_HB_SENT:         return "HB_SENT";
        case CLOUD_STATUS_HB_FAILED:       return "HB_FAILED";
        default:                           return "UNKNOWN";
    }
}

void ModuleSimBridge::_send_json(const char *id, const char *data_json)
{
    mac_driver_send_json(id, data_json);
}

void ModuleSimBridge::_send_pool_status()
{
    char buf[768];
    int  pos = 0;
    int  rem = (int)sizeof(buf);

#define APPEND(...) do { int w = snprintf(buf+pos, (size_t)rem, __VA_ARGS__); \
                         if (w > 0) { pos += w; rem -= w; } } while(0)

    APPEND("{\"classes\":[");

    uint8_t idx = 0;
    hsys_pool_class_info_t info;
    bool first = true;

    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        uint16_t used = info.total_count - info.free_count;
        if (!first) APPEND(",");
        first = false;
        APPEND("{\"idx\":%u,\"block_size\":%u,\"total\":%u,\"free\":%u,\"used\":%u}",
               (unsigned)idx,
               (unsigned)info.block_size,
               (unsigned)info.total_count,
               (unsigned)info.free_count,
               (unsigned)used);
        ++idx;
    }

    APPEND("],");

    hsys_msg_header_pool_info_t hdr;
    hsys_msg_get_header_pool_info(&hdr);

    APPEND("\"hdr\":{\"total\":%u,\"free\":%u,\"used\":%u,\"peak\":%u},",
           (unsigned)hdr.total_slots,
           (unsigned)hdr.free_slots,
           (unsigned)hdr.used_slots,
           (unsigned)hdr.peak_used_slots);

    APPEND("\"ts_ms\":%llu}", (unsigned long long)pal_time_get_ms());

#undef APPEND

    mac_driver_send_json("SIM_POOL_STATUS", buf);
}
