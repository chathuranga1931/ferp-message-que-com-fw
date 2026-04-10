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
#include "app_msg_table.h"

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
};
#define TASK_TABLE_SIZE  (sizeof(k_task_table) / sizeof(k_task_table[0]))

// ============================================================================
// app_main
// ============================================================================

extern "C" void app_init(void)
{
    printf("\n=== HSYS Messaging Architecture Demo ===\n\n");

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
