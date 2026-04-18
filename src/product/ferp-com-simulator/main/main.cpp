/**
 * @file main.cpp
 * @brief Simulator entry point.
 *
 * This is the ONLY simulator-specific setup file.
 *
 * Everything else is shared with the ESP-IDF target:
 *   - All HSYS framework code   (src/sub-modules/hsys-framework/)
 *   - All PAL interfaces        (src/sub-modules/pal/*.h)
 *   - All PAL mac-pc impls      (src/sub-modules/pal/mac-pc/)
 *   - All app-modules           (src/app-modules/)
 *   - All app-messages          (src/app-messages/)
 *   - All app-pheripherals      (src/app-pheripherals/)
 *   - Middleware                (src/sub-modules/middleware/)
 *
 * Simulator-only additions (this product folder only):
 *   - sim_bridge/               TCP socket <-> Python UI bridge module
 *   - main/main.cpp             This file: tables + run loop
 */

#include "pal_logger.h"
#include "module_sim_bridge.h"

// app_config_init() is defined in app.cpp — initialises config defaults
// and the config handle before the module lifecycle starts.
extern "C" void app_config_init(void);

/* HSYS architecture */
#include "hsys_pool.h"
#include "hsys_module.h"
#include "hsys_msg.h"
#include "hsys_task_mgr.h"

/* App modules */
#include "ticker.h"
#include "module_sysmon.h"
#include "module_spiffs.h"
#include "module_config.h"
#include "app_msg_table.h"

#include <cstdio>
#include <time.h>
#include <unistd.h>

#define __TAG__ "SIM_MAIN"
#ifndef SIM_MAIN_LOG_EN
#define SIM_MAIN_LOG_EN true
#endif

// ── Pool class table ──────────────────────────────────────────────────────────

static const hsys_pool_class_cfg_t k_pool_table[] = {
    {   4,   8 },
    {  32,  16 },
    {  64,  16 },
    { 256,   8 },
    { 512,   4 },
};
#define POOL_TABLE_SIZE  (sizeof(k_pool_table) / sizeof(k_pool_table[0]))

// ── Module table ──────────────────────────────────────────────────────────────

static HsysModule *k_module_table[] = {
    ticker_instance(),
    module_sysmon_instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleSimBridge::instance(),   // simulator-only
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ── Task table ────────────────────────────────────────────────────────────────

static const hsys_task_desc_t k_task_table[] = {
    { "ticker_task",     1024,  6,  0,  { TICKER_MODULE_ID,      0 } },
    { "sysmon_task",     2048,  3,  0,  { SYSMON_MODULE_ID,      0 } },
    { "spiffs_task",     2048,  5,  0,  { MODULE_SPIFFS_ID,      0 } },
    { "config_task",     4096,  5,  0,  { MODULE_CONFIG_ID,      0 } },
    { "sim_bridge_task", 4096,  2,  0,  { SIM_BRIDGE_MODULE_ID,  0 } },
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

// ── Entry point ───────────────────────────────────────────────────────────────

int main()
{
    pal_logger_init();
    LOG_MSG_INFO(SIM_MAIN_LOG_EN, "starting ferp-com-simulator");

    // Change working directory to the simulator source folder so that the
    // SPIFFS emulation (pal_mac_spiffs.cpp) always resolves paths relative
    // to  <simulator>/SPIFFS/spiffs/  regardless of where the binary is run.
#ifdef SIMULATOR_SOURCE_DIR
    if (chdir(SIMULATOR_SOURCE_DIR) != 0) {
        LOG_MSG_WARNING(SIM_MAIN_LOG_EN, "chdir to " SIMULATOR_SOURCE_DIR " failed — SPIFFS paths may be wrong");
    } else {
        LOG_MSG_INFO(SIM_MAIN_LOG_EN, "working directory set to " SIMULATOR_SOURCE_DIR);
    }
#endif

    // Start TCP server BEFORE any module init so the bridge is ready
    // to accept connections as soon as post_init messages start flowing.
    ModuleSimBridge::instance()->start_server(9000);

    // 0. Config — load defaults and initialise the config handle so
    //    ModuleConfig can use it when SPIFFS_READY fires.
    app_config_init();

    // 1. Memory pool
    hsys_pool_init(k_pool_table, POOL_TABLE_SIZE);

    // 2. Module registry
    hsys_module_init(k_module_table, MODULE_TABLE_SIZE);

    // 3. Message bus + descriptor table
    hsys_msg_init();
    APP_MSG_TABLE_INIT;
    hsys_msg_table_init(k_msg_table, k_msg_table_count);

    // 4. Task manager — lifecycle phases run inside the tasks themselves
    hsys_task_mgr_init(k_task_table, TASK_TABLE_SIZE);

    LOG_MSG_INFO(SIM_MAIN_LOG_EN, "app initialised — entering run loop");

    // macOS: tasks are pthreads; main thread spins at low cost
    while (true) {
        struct timespec ts = { 0, 10000000 };   // 10 ms
        nanosleep(&ts, nullptr);
    }

    return 0;
}
