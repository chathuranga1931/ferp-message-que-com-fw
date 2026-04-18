/*
 * hello_world_main.cpp — Firmware entry point
 *
 * Message flow:
 *
 *   [Ticker soft-timer]
 *         │  MSG_TICK_1000MS (no payload)
 *         ▼
 *   [ModuleA::on_msg_received]  ←─ sensor_task inbox queue
 *         │  MSG_SENSOR_DATA (module_a_sensor_data_t payload)
 *         ▼
 *   [ModuleB::on_msg_received]  ←─ display_task inbox queue
 *
 * Startup sequence (main only creates tables and calls framework inits):
 *
 *   1. hsys_pool_init()        — configure memory pool classes
 *   2. hsys_module_init()      — register all modules from k_module_table
 *   3. hsys_msg_init()         — initialise the message bus
 *   3b. hsys_msg_table_init()  — register app message descriptors
 *   4. hsys_task_mgr_init()    — pass k_task_table; initialises state + creates all tasks
 *
 * Each task's dispatch loop then runs the three lifecycle phases
 * (pre_init → init → post_init) automatically, with a global
 * barrier ensuring all tasks complete each phase before any task proceeds
 * to the next.  No lifecycle calls are made from main.
 */

#include <stdio.h>
#include <string.h>

/* Own header — ensures extern "C" linkage matches the declaration */
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
#include "app_msg_table.h"
#include "app_config.h"
#include "hsys_config.h"
#include "hsys_type.h"

// ============================================================================
// Device configuration — single in-memory instance
// ============================================================================

app_config_t _app_config;

// ---------------------------------------------------------------------------
// Default values — applied before any file load
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Config field table — maps JSON keys to struct fields.
// Type is config_t from hsys_config.h (name[32], hsys_type_t, void*, uint32_t max_length).
// Passed to hsys_config_init() → config_handle_t for save/load.
// ---------------------------------------------------------------------------

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

config_handle_t * app_config_get_handle(void)
{
    return &g_config_handle;
}

config_t * app_config_get_table(uint16_t * out_size)
{
    if (out_size) *out_size = (uint16_t)CONFIG_TABLE_SIZE;
    return k_config_table;
}

// ============================================================================
// Pool class table
// Columns: block_size (bytes) | block_count
// Must be in ascending block_size order.
// ============================================================================

/*                                        size   count  */
static const hsys_pool_class_cfg_t k_pool_table[] = {
    {   4,   8 },   /* tiny flags / heartbeat acks */
    {  32,  16 },   /* small sensor readings        */
    {  64,  16 },   /* medium payloads              */
    { 256,   8 },   /* large payloads               */
    { 512,   4 },   /* bulk transfers               */
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// ============================================================================
// Module table  — passed to hsys_module_init()
// All module instances are static singletons; no heap allocation.
// ============================================================================

static HsysModule *k_module_table[] = {
    ticker_instance(),
    module_a_instance(),
    module_b_instance(),
    module_sysmon_instance(),
    ModuleSpiffs::instance(),
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ============================================================================
// Task table  — task config + module binding in one place
// Columns: name | stack(words) | priority | queue_depth | modules[]
// ============================================================================

/*                         name            stack  pri  qdepth  modules[]              */
static const hsys_task_desc_t k_task_table[] = {
    { "ticker_task",   1024,  6,  0,  { TICKER_MODULE_ID,   0 } },
    { "sensor_task",   2048,  5,  0,  { MODULE_A_ID,        0 } },
    { "display_task",  2048,  4,  0,  { MODULE_B_ID,        0 } },
    { "sysmon_task",   2048,  3,  0,  { SYSMON_MODULE_ID,   0 } },
    { "spiffs_task",   2048,  5,  0,  { MODULE_SPIFFS_ID,   0 } },
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

// ============================================================================
// app_config_init — load defaults + initialise the config handle
// Called by app_init() and directly by the simulator's main().
// ============================================================================

extern "C" void app_config_init(void)
{
    app_config_load_defaults(&_app_config);
    config_init_t cfg_init = { (uint16_t)CONFIG_TABLE_SIZE, k_config_table };
    hsys_config_init(cfg_init, &g_config_handle);
}

// ============================================================================
// app_main
// ============================================================================

extern "C" void app_init(void)
{
    printf("\n=== HSYS Messaging Architecture Demo ===\n\n");

    // 0. Device config — load defaults then initialise the hsys_config handle
    app_config_init();

    // 1. Memory pool
    hsys_pool_init(k_pool_table, POOL_TABLE_SIZE);

    // 2. Module registry — registers all instances in one call
    hsys_module_init(k_module_table, MODULE_TABLE_SIZE);

    // 3. Message bus + descriptor table
    hsys_msg_init();

    APP_MSG_TABLE_INIT;
    hsys_msg_table_init(k_msg_table, k_msg_table_count);

    // 4. Task manager — initialises shared state and creates all tasks.
    //    Each task's dispatch loop runs the three lifecycle phases
    //    automatically before entering its message loop.
    hsys_task_mgr_init(k_task_table, TASK_TABLE_SIZE);

    printf("\n[main] Tasks created. Lifecycle phases running in tasks.\n\n");

    /* app_main returns — FreeRTOS scheduler takes over */
}

extern "C" void app_run(void)
{
    
}
