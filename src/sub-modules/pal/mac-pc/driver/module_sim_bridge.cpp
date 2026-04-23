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
// ── Sensor / data ──
#include "msg_sensor_data.h"
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
#include "msg_config_get.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_dt.h"
// ── Fuel / dispenser ──
#include "msg_default_btn.h"
#include "msg_printer_btn.h"
#include "msg_nozzle_state.h"
#include "msg_fuel_pumped.h"
// ── Connectivity ──
#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_cloud_status.h"

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
    // ── Sensor / data ──
    subscribe(MSG_ID_SENSOR_DATA);
    // ── Timer (notifications only; responses/alarms are DIRECT) ──
    subscribe(MSG_ID_TIMER_START);
    subscribe(MSG_ID_TIMER_STOP);
    // ── System / timing ──
    // subscribe(MSG_ID_TICK_1000MS);
    subscribe(MSG_ID_SPIFFS_READY);
    subscribe(MSG_ID_SD_READY);
    subscribe(MSG_ID_SD_STATUS);
    subscribe(MSG_ID_TIME_STATUS);
    // ── Config ──
    subscribe(MSG_ID_CONFIG_READY);
    subscribe(MSG_ID_CONFIG_SET);
    subscribe(MSG_ID_CONFIG_GET);
    subscribe(MSG_ID_CONFIG_GET_WIFI);
    subscribe(MSG_ID_CONFIG_GET_CLOUD);
    subscribe(MSG_ID_CONFIG_GET_MQTT);
    subscribe(MSG_ID_CONFIG_GET_DT);
    // ── Fuel / dispenser / buttons ──
    subscribe(MSG_ID_DEFAULT_BTN);
    subscribe(MSG_ID_PRINTER_BTN);
    subscribe(MSG_ID_NOZZLE_STATE);
    subscribe(MSG_ID_FUEL_PUMPED);
    // ── Connectivity ──
    subscribe(MSG_ID_WIFI_EVENT);
    subscribe(MSG_ID_INTERNET_STATUS);
    subscribe(MSG_ID_CLOUD_STATUS);

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
        // ── Sensor / data ─────────────────────────────────────────────────

        case MSG_ID_SENSOR_DATA: {
            auto p = MsgSensorData::deserialize(msg);
            snprintf(data, sizeof(data),
                     "{\"counter\":%u,\"temperature\":%.2f}",
                     (unsigned)p.counter, (double)p.temperature);
            _send_json("MSG_SENSOR_DATA", data);
            break;
        }

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
            auto p = MsgConfigSet::deserialize(msg);
            char val[160];
            switch (p.type) {
                case HSYS_TYPE_BOOL:
                    snprintf(val, sizeof(val), "%s",
                             p.value.as_bool ? "true" : "false");
                    break;
                case HSYS_TYPE_UINT32:
                    snprintf(val, sizeof(val), "%u",
                             (unsigned)p.value.as_uint32);
                    break;
                default:  // HSYS_TYPE_STRING
                    snprintf(val, sizeof(val), "\"%s\"", p.value.as_str);
                    break;
            }
            snprintf(data, sizeof(data),
                     "{\"key\":\"%s\",\"value\":%s}", p.key, val);
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

        case MSG_ID_CLOUD_STATUS: {
            auto p = MsgCloudStatus::deserialize(msg);
            static const auto _cs_str = [](cloud_status_event_t e) -> const char * {
                switch (e) {
                    case CLOUD_STATUS_REGISTERED:      return "REGISTERED";
                    case CLOUD_STATUS_REGISTER_FAILED: return "REGISTER_FAILED";
                    case CLOUD_STATUS_PUMPED_SUCCESS:  return "PUMPED_SUCCESS";
                    case CLOUD_STATUS_PUMPED_FAILED:   return "PUMPED_FAILED";
                    case CLOUD_STATUS_HB_SENT:         return "HB_SENT";
                    case CLOUD_STATUS_HB_FAILED:       return "HB_FAILED";
                    default:                           return "UNKNOWN";
                }
            };
            snprintf(data, sizeof(data),
                     "{\"event\":\"%s\",\"nozzle_idx\":%u}",
                     _cs_str(p.event), (unsigned)p.nozzle_idx);
            _send_json("MSG_CLOUD_STATUS", data);
            strncpy(_last_cloud_json, data, sizeof(_last_cloud_json) - 1);
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
        mac_driver_send_json("MSG_CLOUD_STATUS", b._last_cloud_json);
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
