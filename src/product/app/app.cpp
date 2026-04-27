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

/* HSYS architecture */
#include "hsys_pool.h"
#include "hsys_module.h"
#include "hsys_msg.h"
#include "hsys_task_mgr.h"

/* Application modules */
#include "ticker.h"
#include "module_a.h"
#include "module_b.h"
#include "module_sysmon.h"
#include "module_spiffs.h"
#include "module_config.h"
#include "module_timer.h"
#include "module_leds.h"
#include "module_default_btn.h"
#include "module_print_btn.h"
#include "module_fuel.h"
#include "module_buzzer.h"
#include "module_cloud.h"
#include "cube_sphere_cloud_driver.h"
#include "module_internet.h"
#include "module_wifi.h"
#include "module_sd.h"
#include "module_timemgr.h"
#include "module_ota.h"

// ── OTA platform configuration ─────────────────────────────────────────────
// ota_platform_get_config() wires the source/target tables for OtaModule.
// The target driver (ota_driver_esp32_main) is platform-agnostic: it calls
// pal_fw_update_* which maps to ESP-IDF OTA partitions on device and to a
// host-file stream (<cwd>/ota_download.bin) on the simulator.
// OtaModule stores only pointers — static storage duration required.
#include "ota_driver_esp32_main.h"
#include "app_module_ids.h"

#ifndef MODULE_MQTT_ID
#define MODULE_MQTT_ID  ((hsys_module_id_t)0xFF)   // placeholder — not yet implemented
#endif

static const ota_source_desc_t k_ota_sources[] = {
    // source_module_id      priority  _pad  timeout_ms
    { MODULE_MQTT_ID,        0,        0,    60000  },
    { MODULE_WEB_SERVER_ID,  0,        0,    120000 },  ///< simulator web OTA (port 8080)
};

static ota_esp32_ctx_t s_esp32_ota_ctx = {};

static const ota_target_desc_t k_ota_targets[] = {
    {
        .target_idx   = 0,
        .needs_reboot = true,
        ._pad         = {},
        .label        = "esp32-main",
        .driver       = &g_ota_driver_esp32_main,
        .ctx          = &s_esp32_ota_ctx,
    },
};

#define OTA_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

extern "C" void ota_platform_get_config(
    const ota_source_desc_t **sources, uint8_t *source_count,
    const ota_target_desc_t **targets, uint8_t *target_count)
{
    *sources      = k_ota_sources;
    *source_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_sources);
    *targets      = k_ota_targets;
    *target_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_targets);
}

#include "app_msg_table.h"
#include "app_config.h"
#include "hsys_config.h"
#include "hsys_type.h"

// ============================================================================
// Device configuration — single in-memory instance
// ============================================================================

app_config_t _app_config;

void app_config_load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->wifi_ssid,          "MyNetwork",            sizeof(cfg->wifi_ssid)          - 1);
    strncpy(cfg->wifi_password,      "password123",          sizeof(cfg->wifi_password)       - 1);
    strncpy(cfg->cloud_url,          "https://cloud.example.com", sizeof(cfg->cloud_url)      - 1);
    strncpy(cfg->cloud_secret,       "changeme",             sizeof(cfg->cloud_secret)        - 1);
    cfg->cloud_hb_enabled            = true;
    cfg->cloud_hb_interval_s         = 60;
    strncpy(cfg->mqtt_host,          "mqtt.example.com",     sizeof(cfg->mqtt_host)           - 1);
    cfg->mqtt_port                   = 1883;
    strncpy(cfg->mqtt_user,          "",                     sizeof(cfg->mqtt_user)           - 1);
    strncpy(cfg->mqtt_password,      "",                     sizeof(cfg->mqtt_password)       - 1);
    strncpy(cfg->device_uuid,        "00000000-0000-0000-0000-000000000000", sizeof(cfg->device_uuid) - 1);
    strncpy(cfg->device_group,       "default",              sizeof(cfg->device_group)        - 1);
    cfg->display_type                = 0;
    cfg->stabilize_delay_ms          = 500;
    strncpy(cfg->printer_url,        "http://printer.local", sizeof(cfg->printer_url)         - 1);
    cfg->printer_copy_count          = 1;
    cfg->log_udp_enabled             = false;
    strncpy(cfg->log_udp_server_ip,  "192.168.1.100",        sizeof(cfg->log_udp_server_ip)   - 1);
    cfg->log_udp_port                = 4444;
}

static config_t k_config_table[] = {
    { "ssid",          HSYS_TYPE_STRING, _app_config.wifi_ssid,           sizeof(_app_config.wifi_ssid)           },
    { "password",      HSYS_TYPE_STRING, _app_config.wifi_password,       sizeof(_app_config.wifi_password)       },
    { "cloud_url",     HSYS_TYPE_STRING, _app_config.cloud_url,           sizeof(_app_config.cloud_url)           },
    { "cloud_secret",  HSYS_TYPE_STRING, _app_config.cloud_secret,        sizeof(_app_config.cloud_secret)        },
    { "display_type",  HSYS_TYPE_UINT32, &_app_config.display_type,       sizeof(_app_config.display_type)        },
    { "printer_url",   HSYS_TYPE_STRING, _app_config.printer_url,         sizeof(_app_config.printer_url)         },
    { "p_cpy_cnt",     HSYS_TYPE_UINT32, &_app_config.printer_copy_count, sizeof(_app_config.printer_copy_count)  },
    { "hb_interval",   HSYS_TYPE_UINT32, &_app_config.cloud_hb_interval_s,sizeof(_app_config.cloud_hb_interval_s) },
    { "mqtt_host",     HSYS_TYPE_STRING, _app_config.mqtt_host,           sizeof(_app_config.mqtt_host)           },
    { "mqtt_port",     HSYS_TYPE_UINT32, &_app_config.mqtt_port,          sizeof(_app_config.mqtt_port)           },
    { "mqtt_user",     HSYS_TYPE_STRING, _app_config.mqtt_user,           sizeof(_app_config.mqtt_user)           },
    { "mqtt_pass",     HSYS_TYPE_STRING, _app_config.mqtt_password,       sizeof(_app_config.mqtt_password)       },
    { "device_uuid",   HSYS_TYPE_STRING, _app_config.device_uuid,         sizeof(_app_config.device_uuid)         },
    { "device_group",  HSYS_TYPE_STRING, _app_config.device_group,        sizeof(_app_config.device_group)        },
    { "cptr_delay",    HSYS_TYPE_UINT32, &_app_config.stabilize_delay_ms, sizeof(_app_config.stabilize_delay_ms)  },
    { "en_udp_ser",    HSYS_TYPE_BOOL,   &_app_config.log_udp_enabled,    sizeof(_app_config.log_udp_enabled)     },
    { "udp_srvr_ip",   HSYS_TYPE_STRING, _app_config.log_udp_server_ip,   sizeof(_app_config.log_udp_server_ip)   },
    { "udp_srvr_port", HSYS_TYPE_UINT32, &_app_config.log_udp_port,       sizeof(_app_config.log_udp_port)        },
};
#define CONFIG_TABLE_SIZE  (sizeof(k_config_table) / sizeof(k_config_table[0]))

static config_handle_t g_config_handle;

config_handle_t *app_config_get_handle(void)
{
    return &g_config_handle;
}

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
// Pool class table
// ============================================================================

static const hsys_pool_class_cfg_t k_pool_table[] = {
    {   4,   8 },
    {  32,  32 },
    {  64,  32 },
    { 256,  24 },
    { 512,   8 },
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// ============================================================================
// Shared module table
// Modules common to ALL product targets.
// Platform-only modules are injected via app_register_extra_module().
// ============================================================================

static HsysModule *k_module_table[] = {
    ticker_instance(),
    // module_a_instance(),   // demo module — disabled (Sprint 12 cleanup)
    // module_b_instance(),   // demo module — disabled (Sprint 12 cleanup)
    module_sysmon_instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleTimer::instance(),
    ModuleLeds::instance(),
    ModuleDefaultBtn::instance(),
    ModulePrintBtn::instance(),
    ModuleFuel::instance(),
    ModuleBuzzer::instance(),
    ModuleCloud::instance(),
    ModuleInternet::instance(),
    ModuleWifi::instance(),
    ModuleSD::instance(),
    ModuleTimeMgr::instance(),
    OtaModule::instance(),
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ============================================================================
// Shared task table
// ============================================================================

static const hsys_task_desc_t k_task_table[] = {
    // stack notes (ESP32 Xtensa, FreeRTOS):
    //   storage_task  : SPIFFS/SD mount + JSON config. JSON buf is static → 4096 is fine.
    //   timing_task   : tick counters + timer slot management. Very lightweight.
    //   indicator_task: Sysmon report (vprintf loop) + GPIO LEDs/buzzer.
    //                   ESP-IDF vprintf needs ~512 B; Xtensa FreeRTOS frame ~320 B → min 2048.
    //   btn_task      : debounce state machine + message publish.
    //                   Same logging headroom rule → min 2048.
    //   fuel_task     : sanki6 queue drain + nested state-machine calls.
    //                   Large locals in _process_queues() are static → 4096 is fine.
    //   network_task  : WiFi connect + ICMP ping + HTTPS via esp_http_client + mbedTLS.
    //                   mbedTLS TLS handshake alone ~4-6 KB; ESP-IDF recommends ≥8192 for HTTPS.
    { "storage_task",       4096,  5,  0,   { MODULE_SPIFFS_ID,      MODULE_SD_ID,           MODULE_CONFIG_ID,   0 } },
    { "timing_task",        2048,  4,  0,   { TICKER_MODULE_ID,      MODULE_TIMER_ID,                            0 } },
    { "indicator_task",     2048,  4,  0,   { MODULE_SYSMON_ID,      MODULE_LEDS_ID,         MODULE_BUZZER_ID,   0 } },
    { "btn_task",           2048,  5,  0,   { MODULE_PRINT_BTN_ID,   MODULE_DEFAULT_BTN_ID,                      0 } },
    { "fuel_task",          4096,  5,  0,   { MODULE_FUEL_ID,                                                    0 } },
    { "network_task",       8192,  5,  0,   { MODULE_WIFI_ID,        MODULE_INTERNET_ID,     MODULE_CLOUD_ID,    0 } },
    { "timemgr_task",       3072,  5,  0,   { MODULE_TIMEMGR_ID,                                                 0 } },
    { "ota_task",           4096,  5,  0,   { MODULE_OTA_ID,                                                     0 } },
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
extern "C" __attribute__((weak)) void app_run(void) {}

// ============================================================================
// app_config_init
// ============================================================================

extern "C" void app_config_init(void)
{
    app_config_load_defaults(&_app_config);
    config_init_t cfg_init = { (uint16_t)CONFIG_TABLE_SIZE, k_config_table };
    hsys_config_init(cfg_init, &g_config_handle);
}

// ============================================================================
// app_init
// ============================================================================

extern "C" void app_init(void)
{
    // 0a. PAL system init — platform-level boot (TCP server on simulator, no-op on ESP-IDF)
    //     Must run before anything else so the UI port is open from the very first log line.
    pal_system_init();

    // 0b. Platform-specific setup + extra module registration
    app_platform_pre_init();

    // Wire the concrete cloud backend before modules are initialised.
    // The cube_sphere driver singleton is defined in cube_sphere_cloud_driver.cpp.
    ModuleCloud::instance()->set_driver(cloud_driver_cube_sphere());

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

