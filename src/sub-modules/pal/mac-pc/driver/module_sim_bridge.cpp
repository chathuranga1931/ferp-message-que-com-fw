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
#include "hsys_type.h"
#include "pal_time.h"
// ── Timer ──
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
// ── System / timing ──
#include "msg_tick_1000ms.h"
#include "msg_spiffs_ready.h"
#include "msg_sd_ready.h"
#include "msg_sd_status.h"
#include "msg_time_status.h"
// ── Config ──
#include "msg_config_ready.h"
#include "msg_config_set.h"
#include "msg_config_value.h"
#include "msg_config_get.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_dt.h"
#include "msg_config_get_key.h"
// ── Fuel / dispenser ──
#include "msg_default_btn.h"
#include "msg_printer_btn.h"
#include "msg_nozzle_state.h"
#include "msg_fuel_pumped.h"
// ── System reboot ──
#include "msg_system_reboot.h"
// ── Connectivity ──
#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_cubesphere_status.h"
#include "msg_mqtt_status.h"

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <thread>

// ─── Singleton ───────────────────────────────────────────────────────────────

static ModuleSimBridge s_instance;
ModuleSimBridge *ModuleSimBridge::instance() { return &s_instance; }

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void ModuleSimBridge::init()
{
    // ── Timer (notifications only; responses/alarms are DIRECT) ──
    auto _sub = [this](hsys_msg_id_t id) {
        hsys_status_t st = subscribe(id);
        if (st != HSYS_OK && st != HSYS_ERR_ALREADY_EXISTS)
            fprintf(stderr, "[DBG] sim_bridge subscribe(0x%04X) FAILED st=%d\n", (unsigned)id, (int)st);
    };
    _sub(MSG_ID_TIMER_START);
    _sub(MSG_ID_TIMER_STOP);
    // ── System / timing ──
    // _sub(MSG_ID_TICK_1000MS);
    _sub(MSG_ID_SPIFFS_READY);
    _sub(MSG_ID_SD_READY);
    _sub(MSG_ID_SD_STATUS);
    _sub(MSG_ID_TIME_STATUS);
    // ── Config ──
    _sub(MSG_ID_CONFIG_READY);
    _sub(MSG_ID_CONFIG_SET);
    _sub(MSG_ID_CONFIG_GET);
    _sub(MSG_ID_CONFIG_GET_WIFI);
    _sub(MSG_ID_CONFIG_GET_CLOUD);
    _sub(MSG_ID_CONFIG_GET_MQTT);
    _sub(MSG_ID_CONFIG_GET_DT);
    _sub(MSG_ID_CONFIG_GET_KEY);
    _sub(MSG_ID_CONFIG_VALUE);
    // ── Fuel / dispenser / buttons ──
    _sub(MSG_ID_DEFAULT_BTN);
    _sub(MSG_ID_PRINTER_BTN);
    _sub(MSG_ID_NOZZLE_STATE);
    _sub(MSG_ID_FUEL_PUMPED);
    // ── System reboot ──
    _sub(MSG_ID_SYSTEM_REBOOT);
    // ── Connectivity ──
    _sub(MSG_ID_WIFI_EVENT);
    _sub(MSG_ID_INTERNET_STATUS);
    _sub(MSG_ID_CUBESPHERE_STATUS);
    _sub(MSG_ID_MQTT_STATUS);

    mac_driver_set_connect_cb(&ModuleSimBridge::on_ui_connected);

    // Send pool status every 500 ms on a dedicated thread.
    _pool_thread = std::thread([this]() {
        while (!_pool_stop.load()) {
            _send_pool_status();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
    _pool_thread.detach();

    log("init");
}

// ─── Message handler ─────────────────────────────────────────────────────────

void ModuleSimBridge::on_msg_received(const hsys_msg_t &msg)
{
    char data[256];

    switch (msg.msg_id)
    {
        // ── Timer ─────────────────────────────────────────────────────────

        case MSG_ID_TIMER_START: {
            auto p = MsgTimerStart::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"module_id\":%u,\"offset_ms\":%u,"
                     "\"duration_ms\":%u,\"repetitive\":%s,\"forced\":%s}",
                     (unsigned)p.source_module_id,
                     (unsigned)p.start_offset_ms,
                     (unsigned)p.duration_ms,
                     p.is_repetitive ? "true" : "false",
                     p.forced        ? "true" : "false");
            _send_json("MSG_TIMER_START", data);
            break;
        }

        case MSG_ID_TIMER_STOP: {
            auto p = MsgTimerStop::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"module_id\":%u}", (unsigned)p.source_module_id);
            _send_json("MSG_TIMER_STOP", data);
            break;
        }

        // ── System / timing ───────────────────────────────────────────────

        case MSG_ID_TICK_1000MS:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_TICK_1000MS", data);
            break;

        case MSG_ID_SPIFFS_READY:
            _spiffs_ready = true;
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_SPIFFS_READY", data);
            break;

        case MSG_ID_SD_READY:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_SD_READY", data);
            break;

        case MSG_ID_SD_STATUS: {
            auto p = MsgSdStatus::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"status\":\"%s\",\"card_type\":\"%s\","
                     "\"card_size_mb\":%llu,\"free_mb\":%llu}",
                     p.status_str(), p.card_type,
                     (unsigned long long)p.card_size_mb,
                     (unsigned long long)p.free_mb);
            _send_json("MSG_SD_STATUS", data);
            break;
        }

        case MSG_ID_TIME_STATUS: {
            auto p = MsgTimeStatus::deserialize(msg);
            static const auto _src_str = [](uint8_t s) -> const char * {
                switch (s) {
                    case TIME_SOURCE_NONE:   return "NONE";
                    case TIME_SOURCE_BACKUP: return "BACKUP";
                    case TIME_SOURCE_RTC:    return "RTC";
                    case TIME_SOURCE_NTP:    return "NTP";
                    default:                 return "UNKNOWN";
                }
            };
            snprintf(data, sizeof(data),
                     "{\"epoch\":%lld,\"source\":\"%s\",\"valid\":%s}",
                     (long long)p.epoch, _src_str(p.source),
                     p.valid ? "true" : "false");
            _send_json("MSG_TIME_STATUS", data);
            break;
        }

        // ── Config ────────────────────────────────────────────────────────

        case MSG_ID_CONFIG_READY:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_CONFIG_READY", data);
            break;

        case MSG_ID_CONFIG_SET: {
            // Binary wire format: key(2) type(1) pad(1) size(4) data[]
            uint16_t    key  = MsgConfigSet::get_key(msg);
            uint8_t     type = (uint8_t)MsgConfigSet::get_type(msg);
            uint32_t    sz   = MsgConfigSet::get_data_size(msg);
            const uint8_t *d = (const uint8_t *)MsgConfigSet::get_data(msg);
            // Build compact human-readable JSON for the bridge log
            char vbuf[160] = {};
            if (type == HSYS_TYPE_BOOL && sz >= 1U) {
                snprintf(vbuf, sizeof(vbuf), "%s", d[0] ? "true" : "false");
            } else if (type == HSYS_TYPE_UINT32 && sz >= 4U) {
                uint32_t v = 0; memcpy(&v, d, 4U);
                snprintf(vbuf, sizeof(vbuf), "%u", (unsigned)v);
            } else {
                uint32_t cp = sz < (sizeof(vbuf) - 3U) ? sz : (sizeof(vbuf) - 3U);
                vbuf[0] = '"';
                memcpy(vbuf + 1, d, cp);
                vbuf[cp + 1] = '"';
                vbuf[cp + 2] = '\0';
            }
            snprintf(data, sizeof(data),
                     "{\"key\":0x%04X,\"type\":%u,\"value\":%s}",
                     (unsigned)key, (unsigned)type, vbuf);
            _send_json("MSG_CONFIG_SET", data);
            break;
        }

        case MSG_ID_CONFIG_GET:
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_CONFIG_GET", data);
            break;

        case MSG_ID_CONFIG_GET_WIFI: {
            auto p = MsgConfigGetWifi::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"requester\":%u}", (unsigned)p.source_module_id);
            _send_json("MSG_CONFIG_GET_WIFI", data);
            break;
        }

        case MSG_ID_CONFIG_GET_CLOUD: {
            auto p = MsgConfigGetCloud::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"requester\":%u}", (unsigned)p.source_module_id);
            _send_json("MSG_CONFIG_GET_CLOUD", data);
            break;
        }

        case MSG_ID_CONFIG_GET_MQTT: {
            auto p = MsgConfigGetMqtt::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"requester\":%u}", (unsigned)p.source_module_id);
            _send_json("MSG_CONFIG_GET_MQTT", data);
            break;
        }

        case MSG_ID_CONFIG_GET_DT: {
            auto p = MsgConfigGetDT::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"requester\":%u}", (unsigned)p.source_module_id);
            _send_json("MSG_CONFIG_GET_DT", data);
            break;
        }

        case MSG_ID_CONFIG_GET_KEY: {
            auto p = MsgConfigGetKey::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"key\":%u,\"requester\":%u}",
                     (unsigned)p.key, (unsigned)p.source_module_id);
            _send_json("MSG_CONFIG_GET_KEY", data);
            break;
        }

        case MSG_ID_CONFIG_VALUE: {
            // Forward to Python UI as JSON byte array (same format as HTTP API)
            char json_buf[512] = {};
            int32_t tj = MsgConfigValue::to_json(&msg, json_buf, sizeof(json_buf));
            (void)tj;
            _send_json("MSG_CONFIG_VALUE", json_buf);
            break;
        }

        // ── Fuel / dispenser / buttons ────────────────────────────────────

        case MSG_ID_DEFAULT_BTN: {
            auto p = MsgDefaultBtn::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"status\":\"%s\"}",
                     p.status == BTN_SHORT_PRESS ? "short_press" : "long_press");
            _send_json("MSG_DEFAULT_BTN", data);
            break;
        }

        case MSG_ID_PRINTER_BTN: {
            auto p = MsgPrinterBtn::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"button_id\":%u,\"status\":\"%s\"}",
                     (unsigned)p.button_id,
                     p.status == BTN_SHORT_PRESS ? "short_press" : "long_press");
            _send_json("MSG_PRINTER_BTN", data);
            break;
        }

        case MSG_ID_NOZZLE_STATE: {
            auto p = MsgNozzleState::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"idx\":%u,\"state\":\"%s\"}",
                     (unsigned)p.nozzle_idx,
                     nozzle_state_str(p.state));
            _send_json("MSG_NOZZLE_STATE", data);
            break;
        }

        case MSG_ID_FUEL_PUMPED: {
            auto p = MsgFuelPumped::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"idx\":%u,\"vol_lx1000\":%lu,"
                     "\"unit_pricex100\":%lu,\"total_pricex100\":%lu}",
                     (unsigned)p.nozzle_idx,
                     (unsigned long)p.vol_lx1000,
                     (unsigned long)p.unit_pricex100,
                     (unsigned long)p.total_pricex100);
            _send_json("MSG_FUEL_PUMPED", data);
            break;
        }

        // ── System reboot ─────────────────────────────────────────────────

        case MSG_ID_SYSTEM_REBOOT:
            // Forward to Python before ModuleSysmon calls pal_power_reset().
            // May or may not be delivered depending on task scheduling.
            snprintf(data, sizeof(data), "{}");
            _send_json("MSG_SYSTEM_REBOOT", data);
            break;

        // ── Connectivity ──────────────────────────────────────────────────

        case MSG_ID_WIFI_EVENT: {
            auto p = MsgWifiEvent::deserialize(msg);
            static const auto _ev_str = [](wifi_event_id_t e) -> const char * {
                switch (e) {
                    case WIFI_EVENT_STA_CONNECTED:    return "STA_CONNECTED";
                    case WIFI_EVENT_STA_DISCONNECTED: return "STA_DISCONNECTED";
                    case WIFI_EVENT_STA_GOT_IP:       return "GOT_IP";
                    case WIFI_EVENT_STA_RSSI_CHANGED: return "STA_RSSI_CHANGED";
                    default:                          return "UNKNOWN";
                }
            };
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\",\"rssi\":%d,"
                     "\"ip\":\"%s\",\"ssid\":\"%s\",\"mac\":\"%s\"}",
                     _ev_str(p.event), (int)p.rssi,
                     p.ip_address, p.ssid, p.mac_address);
            _send_json("MSG_WIFI_EVENT", data);
            strncpy(_last_wifi_json, data, sizeof(_last_wifi_json) - 1);
            break;
        }

        case MSG_ID_INTERNET_STATUS: {
            auto p = MsgInternetStatus::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"connected\":%s}", p.connected ? "true" : "false");
            _send_json("MSG_INTERNET_STATUS", data);
            strncpy(_last_internet_json, data, sizeof(_last_internet_json) - 1);
            break;
        }

        case MSG_ID_CUBESPHERE_STATUS: {
            auto p = MsgCubesphereStatus::deserialize(msg);
            static const auto _cs_str = [](cubesphere_status_event_t e) -> const char * {
                switch (e) {
                    case CUBESPHERE_STATUS_REGISTERED:      return "REGISTERED";
                    case CUBESPHERE_STATUS_REGISTER_FAILED: return "REGISTER_FAILED";
                    case CUBESPHERE_STATUS_PUMPED_SUCCESS:  return "PUMPED_SUCCESS";
                    case CUBESPHERE_STATUS_PUMPED_FAILED:   return "PUMPED_FAILED";
                    case CUBESPHERE_STATUS_HB_SENT:         return "HB_SENT";
                    case CUBESPHERE_STATUS_HB_FAILED:       return "HB_FAILED";
                    default:                                return "UNKNOWN";
                }
            };
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\",\"nozzle_idx\":%u,\"device_uuid\":\"%s\"}",
                     _cs_str(p.event), (unsigned)p.nozzle_idx, p.device_uuid);
            _send_json("MSG_CUBESPHERE_STATUS", data);
            strncpy(_last_cloud_json, data, sizeof(_last_cloud_json) - 1);
            break;
        }

        case MSG_ID_MQTT_STATUS: {
            auto p = MsgMqttStatus::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\"}",
                     p.connected ? "CONNECTED" : "DISCONNECTED");
            _send_json("MSG_MQTT_EVENT", data);
            break;
        }

        default:
            break;
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void ModuleSimBridge::_send_json(const char *id, const char *data_json)
{
    mac_driver_send_json(id, data_json);
}

// ─── UI reconnect: replay last known state ────────────────────────────────

/*static*/ void ModuleSimBridge::on_ui_connected()
{
    // Called from the mac_driver accept thread — mac_driver_send_json() is
    // thread-safe, so we can call it directly here.
    auto &b = s_instance;

    if (b._last_wifi_json[0])
        mac_driver_send_json("MSG_WIFI_EVENT", b._last_wifi_json);

    if (b._last_internet_json[0])
        mac_driver_send_json("MSG_INTERNET_STATUS", b._last_internet_json);

    if (b._last_cloud_json[0])
        mac_driver_send_json("MSG_CUBESPHERE_STATUS", b._last_cloud_json);
}

void ModuleSimBridge::_send_pool_status()
{
    // Compact format: {"c":[[used,total,peak],...], "h":[used,total,peak]}
    char buf[256];
    int  pos = 0;
    int  rem = (int)sizeof(buf);

#define APPEND(...) do { int w = snprintf(buf+pos, (size_t)rem, __VA_ARGS__); \
                         if (w > 0) { pos += w; rem -= w; } } while(0)

    APPEND("{\"c\":[");

    uint8_t idx = 0;
    hsys_pool_class_info_t info;
    bool first = true;

    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        uint16_t used = info.total_count - info.free_count;
        if (!first) APPEND(",");
        first = false;
        APPEND("[%u,%u,%u]", (unsigned)used, (unsigned)info.total_count, (unsigned)info.peak_used);
        ++idx;
    }

    hsys_msg_header_pool_info_t hdr;
    hsys_msg_get_header_pool_info(&hdr);

    APPEND("],\"h\":[%u,%u,%u]}",
           (unsigned)hdr.used_slots,
           (unsigned)hdr.total_slots,
           (unsigned)hdr.peak_used_slots);

#undef APPEND

    mac_driver_send_json("SIM_POOL_STATUS", buf);
}
