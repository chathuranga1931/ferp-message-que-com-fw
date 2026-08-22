/**
 * @file main.cpp
 * @brief Simulator entry point — simulator-specific code only.
 *
 * This file contains ONLY what is unique to the macOS simulator target:
 *   1. app_platform_pre_init() — chdir + logger init
 *   2. app_run()               — macOS nanosleep run loop
 *   3. main()                  — calls app_init() then loops on app_run()
 *
 * Everything else lives in shared or PAL files:
 *   - Pool / module / task tables        → src/product/app/app.cpp
 *   - TCP server + ModuleSimBridge reg.  → pal/mac-pc/pal_mac_system.cpp
 *   - All TCP I/O                        → pal/mac-pc/mac_driver.cpp
 */

#include "pal_logger.h"
#include "app.h"
#include "pal_i2c.h"
#include "app_hw_config.h"
#include "pal_mac_i2c_emulator.h"
#include "ds1307_i2c_emulator.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>

// ── Platform hook (overrides weak default in app.cpp) ────────────────────────
//
// Called by app_init() after pal_system_init() (which already started the TCP
// server and registered ModuleSimBridge).  Platform filesystem + logger setup
// and I2C emulator registration happen here.

extern "C" void app_platform_pre_init(void)
{
#ifdef SIMULATOR_SOURCE_DIR
    if (chdir(SIMULATOR_SOURCE_DIR) != 0) {
        printf("[SIM] WARNING: chdir to " SIMULATOR_SOURCE_DIR " failed\n");
    }
#endif

    pal_logger_init();

    /* Register DS1307 RTC emulator — responds to I2C address 0x68 on port 0.
     * ModuleTimeMgr will call ds1307_init() / ds1307_read_time() which use
     * pal_i2c_write_read() routed here.  The emulator reflects host wall time. */
    ds1307_i2c_emulator_init();
    pal_mac_i2c_register_device(APP_HW_I2C_PORT_RTC,
                                APP_HW_I2C_ADDR_DS1307,
                                ds1307_i2c_emulator_get());
}


// ── Entry point ───────────────────────────────────────────────────────────────

int main(void)
{
    app_init();

    while (true) {
        app_run();
    }

    return 0;
}

