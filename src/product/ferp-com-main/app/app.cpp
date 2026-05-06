/*
 * app.cpp — Shared application entry point.
 *
 * This file is compiled into BOTH the simulator and ESP32-IDF targets.
 * It owns:
 *   - Device config (defaults, field table, config handle)
 *   - Shared pool / module / task tables
 *   - app_init()  — full framework initialisation
 *   - app_run()   — default empty run hook (weak; override per platform)
 *   - app_platform_pre_init() — default empty platform hook (weak; override per platform)
 *   - app_register_extra_module() — injects platform-only modules before init
 *
 * Platform-specific additions live in the product main.cpp only:
 *   - app_platform_pre_init() override (chdir, logger init, TCP server, etc.)
 *   - app_run() override (simulator: nanosleep loop)
 *   - Extra modules registered via app_register_extra_module()
 *
 * Startup sequence inside app_init():
 *   0. app_platform_pre_init()  — platform hook (chdir, server start, extra modules)
 *   1. app_config_init()        — load defaults + init config handle
 *   2. hsys_pool_init()         — memory pool
 *   3. hsys_module_init()       — shared modules + any registered extras
 *   4. hsys_msg_init() + table  — message bus + descriptors
 *   5. hsys_task_mgr_init()     — shared tasks + any registered extras
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "pal_system.h"
#include "pal_logger.h"

/* HSYS architecture */
#include "hsys_pool.h"
#include "hsys_module.h"
#include "hsys_msg.h"
#include "hsys_task_mgr.h"

/* Application modules */
#include "ticker.h"
#include "module_sysmon.h"
#include "module_spiffs.h"
#include "module_config.h"
#include "module_timer.h"
#include "module_leds.h"
#include "module_default_btn.h"
#include "module_print_btn.h"
#include "module_fuel.h"
#include "module_buzzer.h"
#include "module_cubesphere.h"
#include "module_internet.h"
#include "module_wifi.h"
#include "module_sd.h"
#include "module_timemgr.h"
#include "module_ota.h"
#include "ModuleWebClientOta.h"
#include "ModuleWebServer.h"
#include "ModuleMqtt.h"
#include "module_device_info.h"
#include "module_plog.h"
#include "module_http.h"

#include "app_ota_config.h"
#include "app_web_ota_config.h"  /* k_web_ota_targets[] + ModuleWebClientOta singleton */

#include "app_msg_table.h"
#include "app_config.h"
#include "app_device_info.h"
#include "app_sd.h"
#include "hsys_config.h"
#include "hsys_type.h"
#include "hsys_task.h"

// Codec registry
#include "app_msg_codec.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_ota.h"
#include "msg_config_set.h"
#include "msg_config_mqtt.h"
#include "msg_config_wifi.h"
#include "msg_fuel_pumped.h"
#include "msg_nozzle_state.h"
#include "msg_internet_status.h"
#include "msg_ota_event.h"
#include "msg_ota_progress.h"
#include "msg_config_cloud.h"
#include "msg_config_ota.h"
#include "msg_mqtt_status.h"
#include "msg_default_btn.h"
#include "msg_printer_btn.h"
#include "msg_sd_ready.h"
#include "msg_sd_status.h"
#include "msg_spiffs_ready.h"
#include "msg_wifi_event.h"
#include "msg_cubesphere_status.h"
#include "msg_time_status.h"
#include "msg_config_ready.h"
#include "msg_config_get.h"
#include "msg_config_get_dt.h"
#include "msg_ota_start_request.h"
#include "msg_ota_abort_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_request_driver.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_stop_response.h"
#include "msg_timer_alarm.h"
#include "msg_tick_1000ms.h"

// ============================================================================
// Device configuration — single in-memory instance
// ============================================================================

app_config_t _app_config;

void app_config_load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    
    strncpy(cfg->wifi_ssid, "FERP-SSID", sizeof(cfg->wifi_ssid) - 1);
    strncpy(cfg->wifi_password, "FERP-PASSWORD", sizeof(cfg->wifi_password) - 1);
    
    strncpy(cfg->cloud_url, "https://cloud.example.com", sizeof(cfg->cloud_url) - 1);
    strncpy(cfg->cloud_secret, "changeme", sizeof(cfg->cloud_secret) - 1);
    cfg->cloud_hb_enabled = true;
    cfg->cloud_hb_interval_s = 60;
    cfg->enable_nid_print = false;
    cfg->enable_nid_cloud = false;

    strncpy(cfg->ota_server_url, "http://144.24.156.245:8080", sizeof(cfg->ota_server_url)  - 1);
    cfg->ota_check_interval_s = 30; 
    
    strncpy(cfg->mqtt_host, "broker.emqx.io", sizeof(cfg->mqtt_host) - 1);
    cfg->mqtt_port = 1883;
    strncpy(cfg->mqtt_user, "", sizeof(cfg->mqtt_user) - 1);
    strncpy(cfg->mqtt_password, "", sizeof(cfg->mqtt_password) - 1);
    
    cfg->display_type = 0;
    cfg->stabilize_delay_ms = 500;
    cfg->tot_cnt = 3;
    cfg->tot_dur = 1000;
    strncpy(cfg->printer_url, "http://printer.local", sizeof(cfg->printer_url) - 1);
    cfg->printer_copy_count = 1;
    cfg->print_delay_ms = 0;
    cfg->en_retx = false;
    cfg->nozzle_swap = false;
    
    cfg->log_udp_enabled = false;
    strncpy(cfg->log_udp_server_ip, "144.24.156.245", sizeof(cfg->log_udp_server_ip) - 1);
    cfg->log_udp_port = 22222;
    
    cfg->dt_log_rate = 1;
    
}

static config_t k_config_table[] = {
    // key                           name             type               ptr                                         size
    { CFG_KEY_WIFI_SSID,           "ssid",          HSYS_TYPE_STRING, _app_config.wifi_ssid,            sizeof(_app_config.wifi_ssid)            },
    { CFG_KEY_WIFI_PASSWORD,       "password",      HSYS_TYPE_STRING, _app_config.wifi_password,        sizeof(_app_config.wifi_password)        },
    { CFG_KEY_CLOUD_URL,           "cloud_url",     HSYS_TYPE_STRING, _app_config.cloud_url,            sizeof(_app_config.cloud_url)            },
    { CFG_KEY_CLOUD_SECRET,        "cloud_secret",  HSYS_TYPE_STRING, _app_config.cloud_secret,         sizeof(_app_config.cloud_secret)         },
    { CFG_KEY_OTA_SERVER_URL,      "ota_srvr_url",  HSYS_TYPE_STRING, _app_config.ota_server_url,       sizeof(_app_config.ota_server_url)       },
    { CFG_KEY_OTA_CHECK_INTERVAL_S,"ota_chk_int",   HSYS_TYPE_UINT32, &_app_config.ota_check_interval_s,sizeof(_app_config.ota_check_interval_s) },
    { CFG_KEY_DISPLAY_TYPE,        "display_type",  HSYS_TYPE_UINT32, &_app_config.display_type,        sizeof(_app_config.display_type)         },
    { CFG_KEY_PRINTER_URL,         "printer_url",   HSYS_TYPE_STRING, _app_config.printer_url,          sizeof(_app_config.printer_url)          },
    { CFG_KEY_PRINTER_COPY_COUNT,  "p_cpy_cnt",     HSYS_TYPE_UINT32, &_app_config.printer_copy_count,  sizeof(_app_config.printer_copy_count)   },
    { CFG_KEY_CLOUD_HB_INTERVAL_S, "hb_interval",   HSYS_TYPE_UINT32, &_app_config.cloud_hb_interval_s, sizeof(_app_config.cloud_hb_interval_s)  },
    { CFG_KEY_MQTT_HOST,           "mqtt_host",     HSYS_TYPE_STRING, _app_config.mqtt_host,            sizeof(_app_config.mqtt_host)            },
    { CFG_KEY_MQTT_PORT,           "mqtt_port",     HSYS_TYPE_UINT32, &_app_config.mqtt_port,           sizeof(_app_config.mqtt_port)            },
    { CFG_KEY_MQTT_USER,           "mqtt_user",     HSYS_TYPE_STRING, _app_config.mqtt_user,            sizeof(_app_config.mqtt_user)            },
    { CFG_KEY_MQTT_PASSWORD,       "mqtt_pass",     HSYS_TYPE_STRING, _app_config.mqtt_password,        sizeof(_app_config.mqtt_password)        },
    { CFG_KEY_STABILIZE_DELAY_MS,  "cptr_delay",    HSYS_TYPE_UINT32, &_app_config.stabilize_delay_ms,  sizeof(_app_config.stabilize_delay_ms)   },
    { CFG_KEY_LOG_UDP_ENABLED,     "en_udp_ser",    HSYS_TYPE_BOOL,   &_app_config.log_udp_enabled,     sizeof(_app_config.log_udp_enabled)      },
    { CFG_KEY_LOG_UDP_SERVER_IP,   "udp_srvr_ip",   HSYS_TYPE_STRING, _app_config.log_udp_server_ip,    sizeof(_app_config.log_udp_server_ip)    },
    { CFG_KEY_LOG_UDP_PORT,        "udp_srvr_port", HSYS_TYPE_UINT32, &_app_config.log_udp_port,        sizeof(_app_config.log_udp_port)         },
    { CFG_KEY_CLOUD_HB_ENABLED,    "hb_enabled",    HSYS_TYPE_BOOL,   &_app_config.cloud_hb_enabled,    sizeof(_app_config.cloud_hb_enabled)     },
    { CFG_KEY_ENABLE_NID_PRINT,    "en_nid_prnt",   HSYS_TYPE_BOOL,   &_app_config.enable_nid_print,    sizeof(_app_config.enable_nid_print)     },
    { CFG_KEY_ENABLE_NID_CLOUD,    "en_nid_cloud",  HSYS_TYPE_BOOL,   &_app_config.enable_nid_cloud,    sizeof(_app_config.enable_nid_cloud)     },
    { CFG_KEY_DT_LOG_RATE,         "dt_log_rate",   HSYS_TYPE_UINT32, &_app_config.dt_log_rate,         sizeof(_app_config.dt_log_rate)          },
    { CFG_KEY_PRINT_DELAY_MS,      "prnt_delay",    HSYS_TYPE_UINT32, &_app_config.print_delay_ms,      sizeof(_app_config.print_delay_ms)       },
    { CFG_KEY_EN_RETX,             "en_retx",       HSYS_TYPE_BOOL,   &_app_config.en_retx,             sizeof(_app_config.en_retx)              },
    { CFG_KEY_NOZZLE_SWAP,         "nzle_swap",     HSYS_TYPE_BOOL,   &_app_config.nozzle_swap,         sizeof(_app_config.nozzle_swap)          },
    { CFG_KEY_TOT_CNT,             "tot_cnt",       HSYS_TYPE_UINT32, &_app_config.tot_cnt,             sizeof(_app_config.tot_cnt)              },
    { CFG_KEY_TOT_DUR,             "tot_dur",       HSYS_TYPE_UINT32, &_app_config.tot_dur,             sizeof(_app_config.tot_dur)              },
};
#define CONFIG_TABLE_SIZE  (sizeof(k_config_table) / sizeof(k_config_table[0]))

const app_config_t *app_config_get(void)
{
    return &_app_config;
}

config_t *app_config_get_table(uint16_t *out_size)
{
    if (out_size) *out_size = (uint16_t)CONFIG_TABLE_SIZE;
    return k_config_table;
}

// ============================================================================
// Device identity — runtime-only, never persisted to flash
// ============================================================================

static app_device_info_t s_device_info = {};

const hsys_module_id_t k_dev_info_perm_cloud_write[]    = { MODULE_CUBESPHERE_ID };
const uint8_t          k_dev_info_perm_cloud_write_count = 1;

static dev_info_entry_t k_dev_info_table[] = {
    {
        DEV_INFO_KEY_DEVICE_UUID,
        "device_uuid",
        k_dev_info_perm_cloud_write, k_dev_info_perm_cloud_write_count,
        nullptr, 0,
        HSYS_TYPE_STRING,
        s_device_info.device_uuid,
        sizeof(s_device_info.device_uuid),
        false
    },
    {
        DEV_INFO_KEY_DEVICE_GROUP,
        "device_group",
        k_dev_info_perm_cloud_write, k_dev_info_perm_cloud_write_count,
        nullptr, 0,
        HSYS_TYPE_STRING,
        s_device_info.device_group,
        sizeof(s_device_info.device_group),
        false
    },
};
#define DEV_INFO_TABLE_SIZE  (sizeof(k_dev_info_table) / sizeof(k_dev_info_table[0]))

app_device_info_t *app_device_info_get(void)
{
    return &s_device_info;
}

dev_info_entry_t *app_device_info_get_table(uint16_t *out_count)
{
    if (out_count) *out_count = (uint16_t)DEV_INFO_TABLE_SIZE;
    return k_dev_info_table;
}

// ============================================================================
// Codec table — JSON serialisation registry (all wire-capable messages).
//
// Transport-agnostic: these rows describe only *how* to encode/decode each
// message type, not *where* it is routed.  Used by MQTT, PLog, sim bridge.
// ============================================================================

static const app_msg_codec_entry_t k_codec_table[] = {
    //  msg_name                     msg_id                         from_json                              to_json

    // ── Config requests ───────────────────────────────────────────────────────
    { "MsgConfigGetMqtt",        MSG_ID_CONFIG_GET_MQTT,       MsgConfigGetMqtt::from_json,        MsgConfigGetMqtt::to_json       },
    { "MsgConfigGetWifi",        MSG_ID_CONFIG_GET_WIFI,       MsgConfigGetWifi::from_json,        MsgConfigGetWifi::to_json       },
    { "MsgConfigGetCloud",       MSG_ID_CONFIG_GET_CLOUD,      MsgConfigGetCloud::from_json,       MsgConfigGetCloud::to_json      },
    { "MsgConfigGetOta",         MSG_ID_CONFIG_GET_OTA,        MsgConfigGetOta::from_json,         MsgConfigGetOta::to_json        },
    { "MsgConfigGetDT",          MSG_ID_CONFIG_GET_DT,         MsgConfigGetDT::from_json,          MsgConfigGetDT::to_json         },
    { "MsgConfigGet",            MSG_ID_CONFIG_GET,            MsgConfigGet::from_json,            MsgConfigGet::to_json           },
    { "MsgConfigSet",            MSG_ID_CONFIG_SET,            MsgConfigSet::from_json,            MsgConfigSet::to_json           },

    // ── Config responses ──────────────────────────────────────────────────────
    { "MsgConfigMqtt",           MSG_ID_CONFIG_MQTT,           MsgConfigMqtt::from_json,           MsgConfigMqtt::to_json          },
    { "MsgConfigWifi",           MSG_ID_CONFIG_WIFI,           MsgConfigWifi::from_json,           MsgConfigWifi::to_json          },
    { "MsgConfigCloud",          MSG_ID_CONFIG_CLOUD,          MsgConfigCloud::from_json,          MsgConfigCloud::to_json         },
    { "MsgConfigOta",            MSG_ID_CONFIG_OTA,            MsgConfigOta::from_json,            MsgConfigOta::to_json           },
    { "MsgConfigReady",          MSG_ID_CONFIG_READY,          MsgConfigReady::from_json,          MsgConfigReady::to_json         },

    // ── Fuel / dispenser ─────────────────────────────────────────────────────
    { "MsgFuelPumped",           MSG_ID_FUEL_PUMPED,           MsgFuelPumped::from_json,           MsgFuelPumped::to_json          },
    { "MsgNozzleState",          MSG_ID_NOZZLE_STATE,          MsgNozzleState::from_json,          MsgNozzleState::to_json         },

    // ── Buttons ───────────────────────────────────────────────────────────────
    { "MsgDefaultBtn",           MSG_ID_DEFAULT_BTN,           MsgDefaultBtn::from_json,           MsgDefaultBtn::to_json          },
    { "MsgPrinterBtn",           MSG_ID_PRINTER_BTN,           MsgPrinterBtn::from_json,           MsgPrinterBtn::to_json          },

    // ── Storage ───────────────────────────────────────────────────────────────
    { "MsgSpiffsReady",          MSG_ID_SPIFFS_READY,          MsgSpiffsReady::from_json,          MsgSpiffsReady::to_json         },
    { "MsgSdReady",              MSG_ID_SD_READY,              MsgSdReady::from_json,              MsgSdReady::to_json             },
    { "MsgSdStatus",             MSG_ID_SD_STATUS,             MsgSdStatus::from_json,             MsgSdStatus::to_json            },

    // ── Connectivity ─────────────────────────────────────────────────────────
    { "MsgWifiEvent",            MSG_ID_WIFI_EVENT,            MsgWifiEvent::from_json,            MsgWifiEvent::to_json           },
    { "MsgInternetStatus",       MSG_ID_INTERNET_STATUS,       MsgInternetStatus::from_json,       MsgInternetStatus::to_json      },
    { "MsgCubesphereStatus",      MSG_ID_CUBESPHERE_STATUS,     MsgCubesphereStatus::from_json,     MsgCubesphereStatus::to_json    },
    { "MsgMqttStatus",           MSG_ID_MQTT_STATUS,           MsgMqttStatus::from_json,           MsgMqttStatus::to_json          },

    // ── Time ─────────────────────────────────────────────────────────────────
    { "MsgTimeStatus",           MSG_ID_TIME_STATUS,           MsgTimeStatus::from_json,           MsgTimeStatus::to_json          },

    // ── OTA lifecycle ─────────────────────────────────────────────────────────
    { "MsgOtaStartRequest",      MSG_ID_OTA_START_REQUEST,     MsgOtaStartRequest::from_json,      MsgOtaStartRequest::to_json     },
    { "MsgOtaAbortRequest",      MSG_ID_OTA_ABORT_REQUEST,     MsgOtaAbortRequest::from_json,      MsgOtaAbortRequest::to_json     },
    { "MsgOtaStartResponse",     MSG_ID_OTA_START_RESPONSE,    MsgOtaStartResponse::from_json,     MsgOtaStartResponse::to_json    },
    { "MsgOtaCompleteNotify",    MSG_ID_OTA_COMPLETE_NOTIFY,   MsgOtaCompleteNotify::from_json,    MsgOtaCompleteNotify::to_json   },
    { "MsgOtaRequestDriver",     MSG_ID_OTA_REQUEST_DRIVER,    MsgOtaRequestDriver::from_json,     MsgOtaRequestDriver::to_json    },
    { "MsgOtaEvent",             MSG_ID_OTA_EVENT,             MsgOtaEvent::from_json,             MsgOtaEvent::to_json            },
    { "MsgOtaProgress",          MSG_ID_OTA_PROGRESS,          MsgOtaProgress::from_json,          MsgOtaProgress::to_json         },

    // ── Timers ────────────────────────────────────────────────────────────────
    { "MsgTimerStart",           MSG_ID_TIMER_START,           MsgTimerStart::from_json,           MsgTimerStart::to_json          },
    { "MsgTimerStop",            MSG_ID_TIMER_STOP,            MsgTimerStop::from_json,            MsgTimerStop::to_json           },
    { "MsgTimerStartResponse",   MSG_ID_TIMER_START_RESPONSE,  MsgTimerStartResponse::from_json,   MsgTimerStartResponse::to_json  },
    { "MsgTimerStopResponse",    MSG_ID_TIMER_STOP_RESPONSE,   MsgTimerStopResponse::from_json,    MsgTimerStopResponse::to_json   },
    { "MsgTimerAlarm",           MSG_ID_TIMER_ALARM,           MsgTimerAlarm::from_json,           MsgTimerAlarm::to_json          },
    { "MsgTick1000ms",           MSG_ID_TICK_1000MS,           MsgTick1000ms::from_json,           MsgTick1000ms::to_json          },
};

// ============================================================================
// MQTT route table — inbound routing policy for ModuleMqtt.
//
// Only messages that ModuleMqtt may *receive* from the wire need an entry.
// Outbound-only messages (those with from_json=nullptr above) are omitted.
//
//   dest_module = 0        → hsys_msg_publish() broadcast (notification)
//   dest_module = <id>     → hsys_msg_send() direct to that module
//   multicast_resp = true  → process even when cmd arrived on wildcard topic
// ============================================================================

static const app_msg_mqtt_route_t k_mqtt_route_table[] = {
    //  msg_id                         dest_module           multicast_resp

    // ── Config requests → config module ──────────────────────────────────────
    { MSG_ID_CONFIG_GET_MQTT,       MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_GET_WIFI,       MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_GET_CLOUD,      MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_GET_OTA,        MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_GET_DT,         MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_GET,            MODULE_CONFIG_ID,     false },
    { MSG_ID_CONFIG_SET,            (hsys_module_id_t)0,  true  }, // broadcast, multicast

    // ── Fuel / dispenser → broadcast ─────────────────────────────────────────
    { MSG_ID_FUEL_PUMPED,           (hsys_module_id_t)0,  false },
    { MSG_ID_NOZZLE_STATE,          (hsys_module_id_t)0,  false },

    // ── Buttons → broadcast ───────────────────────────────────────────────────
    { MSG_ID_DEFAULT_BTN,           (hsys_module_id_t)0,  false },
    { MSG_ID_PRINTER_BTN,           (hsys_module_id_t)0,  false },

    // ── Connectivity → broadcast ─────────────────────────────────────────────
    { MSG_ID_WIFI_EVENT,            (hsys_module_id_t)0,  false },
    { MSG_ID_INTERNET_STATUS,       (hsys_module_id_t)0,  false },

    // ── OTA lifecycle → broadcast ─────────────────────────────────────────────
    { MSG_ID_OTA_START_REQUEST,     (hsys_module_id_t)0,  false },
    { MSG_ID_OTA_ABORT_REQUEST,     (hsys_module_id_t)0,  false },
    { MSG_ID_OTA_START_RESPONSE,    (hsys_module_id_t)0,  false },
    { MSG_ID_OTA_COMPLETE_NOTIFY,   (hsys_module_id_t)0,  false },
    { MSG_ID_OTA_REQUEST_DRIVER,    (hsys_module_id_t)0,  false },

    // ── Timers → timer module ─────────────────────────────────────────────────
    { MSG_ID_TIMER_START,           MODULE_TIMER_ID,      false },
    { MSG_ID_TIMER_STOP,            MODULE_TIMER_ID,      false },
};

// ============================================================================
// Persistent log — auto-logged message ID table
//
// ModulePLog subscribes to every ID listed here at startup and encodes each
// received message to JSON (via app_msg_codec_encode) before writing it to
// the rotating SD-card log.  Only messages that have an encode function in
// k_codec_table above will produce useful JSON; others are silently skipped.
//
// Add or remove IDs freely — no other file needs to change.
// ============================================================================

static const hsys_msg_id_t k_plog_msg_ids[] = {
    // System / storage
    MSG_ID_SPIFFS_READY,        ///< SPIFFS mounted
    MSG_ID_SD_READY,            ///< SD card mounted
    MSG_ID_SD_STATUS,           ///< SD card info (type, size, free)
    MSG_ID_TIME_STATUS,         ///< time source and validity

    // Config lifecycle (startup + live reload)
    MSG_ID_CONFIG_READY,        ///< config loaded or updated

    // Fuel / dispenser
    MSG_ID_FUEL_PUMPED,         ///< complete fueling transaction
    MSG_ID_NOZZLE_STATE,        ///< nozzle lifted / replaced

    // Buttons
    MSG_ID_DEFAULT_BTN,         ///< default button pressed
    MSG_ID_PRINTER_BTN,         ///< print button pressed

    // Connectivity
    MSG_ID_WIFI_EVENT,          ///< WiFi state change
    MSG_ID_INTERNET_STATUS,     ///< internet reachability change
    MSG_ID_CLOUD_STATUS,        ///< cloud event (registered, send OK/fail, …)
    MSG_ID_MQTT_STATUS,         ///< MQTT broker connection state change

    // OTA lifecycle
    MSG_ID_OTA_START_REQUEST,   ///< OTA session requested
    MSG_ID_OTA_START_RESPONSE,  ///< OTA session grant / reject
    MSG_ID_OTA_ABORT_REQUEST,   ///< OTA abort requested
    MSG_ID_OTA_COMPLETE_NOTIFY, ///< OTA binary write finished
    MSG_ID_OTA_EVENT,           ///< OTA session lifecycle event
    MSG_ID_OTA_PROGRESS,        ///< OTA download progress
};
#define PLOG_MSG_TABLE_SIZE  (sizeof(k_plog_msg_ids) / sizeof(k_plog_msg_ids[0]))

static const hsys_pool_class_cfg_t k_pool_table[] = {
    {    4,   8 },
    {   32,  32 },
    {   64,  32 },
    {  256,  24 },
    {  512,   8 },
    { 2048,   2 },   ///< JSON config working buffer (load + save cycle)
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// ============================================================================
// Shared module table
// Modules common to ALL product targets.
// Platform-only modules are injected via app_register_extra_module().
// ============================================================================

static HsysModule *k_module_table[] = {
    Ticker::instance(),
    ModuleSysmon::instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleTimer::instance(),
    ModuleLeds::instance(),
    ModuleDefaultBtn::instance(),
    ModulePrintBtn::instance(),
    ModuleFuel::instance(),
    ModuleBuzzer::instance(),
    ModuleCubeSphere::instance(),
    ModuleInternet::instance(),
    ModuleWifi::instance(),
    ModuleSD::instance(),
    ModuleTimeMgr::instance(),
    OtaModule::instance(),
    // ModuleWebClientOta::instance(),
    ModuleWebServer::instance(),
    ModuleMqtt::instance(),
    ModuleDeviceInfo::instance(),
    ModulePLog::instance(),
    ModuleHttp::instance(),
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ============================================================================
// Shared task table
// ============================================================================

static const hsys_task_desc_t k_task_table[] = {
    // stack notes (ESP32 Xtensa, FreeRTOS):
    //   storage_task  : SPIFFS/SD mount + JSON config. JSON buf is static → 4096 is fine.
    //   timing_task   : tick counters + timer slot management.
    //                   MODULE_TIMEMGR _on_ntp_done() keeps 5+ frames outstanding
    //                   (ds1307 I2C, _write_spiffs_backup, _publish_status, _stop_timer,
    //                   _arm_timer) then calls a final LOG which runs vsnprintf while all
    //                   those frames are still live → ~1400 B + 200 B ISR save. Min 3 KB.
    //   indicator_task: Sysmon report (vprintf loop) + GPIO LEDs/buzzer.
    //                   ESP-IDF vprintf needs ~512 B; Xtensa FreeRTOS frame ~320 B → min 2048.
    //   btn_task      : debounce state machine + message publish.
    //                   Same logging headroom rule → min 2048.
    //   fuel_task      : sanki6 queue drain + nested state-machine calls.
    //                    Large locals in _process_queues() are static → 4096 is fine.
    //   network_task1  : WiFi connect + ICMP ping + MQTT over TLS via mbedTLS → 8 KB.
    //   network_task2  : MODULE_CLOUD_ID is now fully message-driven (no direct TLS).
    //                    MODULE_WEB_CLIENT_OTA_ID still calls pal_http_client directly
    //                    (mbedTLS handshake ~4-6 KB) → keep at 8 KB minimum.
    //   http_task      : ModuleHttp owns all TLS for CubeSphere sessions → 10 KB.
    { "storage_task",     4*1024,  5,  0,   { MODULE_SPIFFS_ID,      MODULE_SD_ID,             MODULE_CONFIG_ID,     MODULE_DEVICE_INFO_ID,  MODULE_PLOG_ID, 0 } },
    { "timing_task",      3*1024,  4,  0,   { TICKER_MODULE_ID,      MODULE_TIMER_ID,          MODULE_TIMEMGR_ID,                            0 } },
    { "indicator_task",   2*1024,  4,  0,   { MODULE_SYSMON_ID,      MODULE_LEDS_ID,           MODULE_BUZZER_ID,                             0 } },
    { "btn_task",         2*1024,  5,  0,   { MODULE_PRINT_BTN_ID,   MODULE_DEFAULT_BTN_ID,                                                  0 } },
    { "fuel_task",        4*1024,  5,  0,   { MODULE_FUEL_ID,                                                                                0 } },
    { "network_task" ,   10*1024,  5,  0,   { MODULE_WIFI_ID,        MODULE_INTERNET_ID,       MODULE_MQTT_ID,                    
                                              MODULE_CLOUD_ID,       MODULE_WEB_CLIENT_OTA_ID, MODULE_WEB_SERVER_ID,  MODULE_OTA_ID,         0 } },
    { "http_task",        5*1024,  5,  0,   { MODULE_HTTP_ID,                                                                                0 } }
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

// sizeof() is unavailable to the C preprocessor, so use static_assert instead.
static_assert(TASK_TABLE_SIZE <= HSYS_MAX_TASKS,
              "TASK_TABLE_SIZE exceeds HSYS_MAX_TASKS; increase HSYS_MAX_TASKS in user_config.h");

// ============================================================================
// Extra module injection (called by app_platform_pre_init)
// ============================================================================

#define APP_MAX_EXTRA_MODULES  8

static HsysModule              *s_extra_modules[APP_MAX_EXTRA_MODULES] = {};
static const hsys_task_desc_t  *s_extra_tasks[APP_MAX_EXTRA_MODULES]   = {};
static uint8_t                  s_extra_count = 0;

extern "C" void app_register_extra_module(HsysModule             *module,
                                           const hsys_task_desc_t *task_desc)
{
    if (!module || !task_desc || s_extra_count >= APP_MAX_EXTRA_MODULES) return;
    s_extra_modules[s_extra_count] = module;
    s_extra_tasks[s_extra_count]   = task_desc;
    s_extra_count++;
}

// ============================================================================
// Platform hooks — weak defaults (override in platform main.cpp)
// ============================================================================

extern "C" __attribute__((weak)) void app_platform_pre_init(void) {}

// ============================================================================
// app_config_init
// ============================================================================

extern "C" void app_config_init(void)
{
    // Load compiled-in defaults into the live config struct.
    // hsys_config_init() is called later by ModuleConfig::init() so that
    // the config handle is owned by the module, not global app state.
    app_config_load_defaults(&_app_config);
}

// ============================================================================
// app_init
// ============================================================================

#ifndef FERP_SIMULATOR
#include "board.h"
#endif
extern "C" void app_init(void)
{
    logger.init();

    // this is for displaytap library only. 
    // but this sets other GPIOs, as inputs output
    // this firmware will overide these settings as per needed. 
#ifndef FERP_SIMULATOR
    board_init();
#endif

    // 0a. PAL system init — platform-level boot (TCP server on simulator, no-op on ESP-IDF)
    //     Must run before anything else so the UI port is open from the very first log line.
    pal_system_init();


    // 0b. Platform-specific setup + extra module registration
    app_platform_pre_init();

    // 0c. Register the JSON message codec table and MQTT routing table
    app_msg_codec_register(k_codec_table,
                           (uint8_t)(sizeof(k_codec_table) / sizeof(k_codec_table[0])));
    app_msg_mqtt_route_register(k_mqtt_route_table,
                                (uint8_t)(sizeof(k_mqtt_route_table) / sizeof(k_mqtt_route_table[0])));

    // Wire SD storage into ModuleCubeSphere for retransmission.
    // retx_mgr_init() is deferred until MsgSdReady is received at runtime.
    ModuleCubeSphere::instance()->set_storage(app_sd_get_storage_interface());

    // Wire SD storage into ModulePLog for persistent log files.
    // Logger activates on MsgSdReady; auto-logs every message ID in k_plog_msg_ids.
    ModulePLog::instance()->set_storage(app_sd_get_storage_interface());
    ModulePLog::instance()->set_msg_table(k_plog_msg_ids, PLOG_MSG_TABLE_SIZE);

    // 1. Config — load defaults and initialise the config handle
    app_config_init();

    // 2. Memory pool
    hsys_pool_init(k_pool_table, POOL_TABLE_SIZE);

    // 3. Module registry — shared modules + platform extras
    {
        HsysModule *all_modules[MODULE_TABLE_SIZE + APP_MAX_EXTRA_MODULES];
        memcpy(all_modules, k_module_table, sizeof(HsysModule *) * MODULE_TABLE_SIZE);
        for (uint8_t i = 0; i < s_extra_count; i++)
            all_modules[MODULE_TABLE_SIZE + i] = s_extra_modules[i];
        hsys_module_init(all_modules, (uint8_t)(MODULE_TABLE_SIZE + s_extra_count));
    }

    // 4. Message bus + descriptor table
    hsys_msg_init();
    APP_MSG_TABLE_INIT;
    hsys_msg_table_init(k_msg_table, k_msg_table_count);

    // 5. Task manager — shared tasks + platform extras
    {
        hsys_task_desc_t all_tasks[TASK_TABLE_SIZE + APP_MAX_EXTRA_MODULES];
        memcpy(all_tasks, k_task_table, sizeof(hsys_task_desc_t) * TASK_TABLE_SIZE);
        for (uint8_t i = 0; i < s_extra_count; i++)
            all_tasks[TASK_TABLE_SIZE + i] = *s_extra_tasks[i];
        hsys_task_mgr_init(all_tasks, (uint8_t)(TASK_TABLE_SIZE + s_extra_count));
    }
}


extern "C" void app_run(void)
{
   hsys_task_delay(1000);
}

