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
    {  32,  16 },
    {  64,  16 },
    { 256,   8 },
    { 512,   4 },
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// ============================================================================
// Shared module table
// Modules common to ALL product targets.
// Platform-only modules are injected via app_register_extra_module().
// ============================================================================

static HsysModule *k_module_table[] = {
    ticker_instance(),
    module_a_instance(),
    module_b_instance(),
    module_sysmon_instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleTimer::instance(),
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ============================================================================
// Shared task table
// ============================================================================

static const hsys_task_desc_t k_task_table[] = {
    { "ticker_task",  1024,  6,  0,  { TICKER_MODULE_ID,    0 } },
    { "sensor_task",  2048,  5,  0,  { MODULE_A_ID,         0 } },
    { "display_task", 2048,  4,  0,  { MODULE_B_ID,         0 } },
    { "sysmon_task",  2048,  3,  0,  { SYSMON_MODULE_ID,    0 } },
    { "spiffs_task",  2048,  5,  0,  { MODULE_SPIFFS_ID,    0 } },
    { "config_task",  4096,  5,  0,  { MODULE_CONFIG_ID,    0 } },
    { "timer_task",   2048,  4,  0,  { MODULE_TIMER_ID,     0 } },
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

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
    // 0. Platform-specific setup + extra module registration
    app_platform_pre_init();

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

