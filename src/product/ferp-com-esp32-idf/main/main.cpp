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
 *   4. hsys_task_mgr_init()    — pass k_task_table; initialises state + creates all tasks
 *
 * Each task's dispatch loop then runs the three lifecycle phases
 * (pre_init → init → post_init) automatically, with a global
 * barrier ensuring all tasks complete each phase before any task proceeds
 * to the next.  No lifecycle calls are made from main.
 */

#include <stdio.h>

#include "app.h"

extern "C" void app_main(void)
{
    app_init();

    while(true) 
    {
        app_run();
    }
}
