/**
 * @file sim_init.cpp
 * @brief Simulator-specific initialisation.
 *
 * Extends the shared `app_init()` pool/module/task tables by appending
 * simulator-only modules (ModuleSimBridge).  The shared `app.cpp` is
 * compiled in unchanged; this file is compiled ONLY for the simulator target.
 *
 * Call order in main.cpp:
 *   1. ModuleSimBridge::instance()->start_server(9000)   ← opens socket
 *   2. sim_app_init()                                    ← this file
 */

#include "sim_init.h"
#include "pal_logger.h"

#define __TAG__ "SIM_INIT"
#ifndef SIM_INIT_LOG_EN
#define SIM_INIT_LOG_EN true
#endif

#include <stdio.h>

/* HSYS architecture */
#include "hsys_pool.h"
#include "hsys_module.h"
#include "hsys_msg.h"
#include "hsys_task_mgr.h"

/* Shared app modules */
#include "ticker.h"
#include "module_sysmon.h"
#include "module_spiffs.h"
#include "module_config.h"
#include "app_msg_table.h"

/* Simulator-only modules */
#include "module_sim_bridge.h"

// ============================================================================
// Pool class table  (same as app.cpp)
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
// Module table  — app modules + sim_bridge
// ============================================================================

static HsysModule *k_module_table[] = {
    ticker_instance(),
    module_sysmon_instance(),
    ModuleSpiffs::instance(),
    ModuleConfig::instance(),
    ModuleSimBridge::instance(),   // ← simulator-only
};
#define MODULE_TABLE_SIZE  (sizeof(k_module_table) / sizeof(k_module_table[0]))

// ============================================================================
// Task table  — sim_bridge gets its own low-priority task
// ============================================================================

static const hsys_task_desc_t k_task_table[] = {
    { "ticker_task",     1024,  6,  0,  { TICKER_MODULE_ID,          0 } },
    { "sysmon_task",     2048,  3,  0,  { MODULE_SYSMON_ID,          0 } },
    { "spiffs_task",     2048,  5,  0,  { MODULE_SPIFFS_ID,          0 } },
    { "config_task",     4096,  5,  0,  { MODULE_CONFIG_ID,          0 } },
    { "sim_bridge_task", 4096,  2,  0,  { SIM_BRIDGE_MODULE_ID,      0 } },
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

// ============================================================================
// sim_app_init  — replaces app_init() for the simulator
// ============================================================================

void sim_app_init(void)
{
    LOG_MSG_INFO(SIM_INIT_LOG_EN, "=== HSYS Messaging Architecture — Simulator ===");

    // 1. Memory pool
    hsys_pool_init(k_pool_table, POOL_TABLE_SIZE);

    // 2. Module registry
    hsys_module_init(k_module_table, MODULE_TABLE_SIZE);

    // 3. Message bus + descriptor table
    hsys_msg_init();
    APP_MSG_TABLE_INIT;
    hsys_msg_table_init(k_msg_table, k_msg_table_count);

    // 4. Task manager — creates all tasks (including sim_bridge_task)
    hsys_task_mgr_init(k_task_table, TASK_TABLE_SIZE);

    LOG_MSG_INFO(SIM_INIT_LOG_EN, "[sim] Tasks created. Lifecycle phases running in tasks.");
}
