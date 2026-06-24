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
#include "pal/pal_i2c.h"
#include "pal/pal_crash_log.h"  // pal_crash_log_disable_boot_wdt

// ---------------------------------------------------------------------------
// Platform hook — runs before the module framework starts.
// Initialise board-level peripherals that modules depend on (I2C, SPI, etc.).
// ---------------------------------------------------------------------------
extern "C" void app_platform_pre_init(void)
{
    // I2C port 0: DS1307 RTC (SDA=GPIO21, SCL=GPIO22, 100 kHz)
    pal_i2c_config_t i2c_cfg = {};
    i2c_cfg.mode              = PAL_I2C_MODE_MASTER;
    i2c_cfg.sda_pin           = 21;
    i2c_cfg.scl_pin           = 22;
    i2c_cfg.clock_speed       = 100000;
    i2c_cfg.sda_pullup_enable = true;
    i2c_cfg.scl_pullup_enable = true;
    pal_i2c_init(PAL_I2C_PORT_0, &i2c_cfg);
}

extern "C" void app_main(void)
{
    // Disable the RWDT before anything else.
    // esp_restart_noos() arms RWDT Stage0=RESET_SYSTEM@1s and Stage1=RESET_RTC@1s
    // before every reboot.  IDF cpu_start.c only clears it for RWDT/MWDT resets;
    // for a software panic (task_wdt → SW_CPU_RESET) it keeps counting through
    // the entire boot.  If logger.init / board_init / pal_system_init push the
    // total boot time past ~2 s, Stage1 fires and wipes all RTC SRAM.
    pal_crash_log_disable_boot_wdt();

    app_init();

    while(true) 
    {
        app_run();
    }
}
