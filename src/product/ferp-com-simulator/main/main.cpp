/**
 * @file main.cpp
 * @brief Simulator entry point — simulator-specific code only.
 *
 * This file contains ONLY what is unique to the macOS simulator target:
 *   1. app_platform_pre_init() — chdir, logger init, TCP server, sim_bridge registration
 *   2. app_run()               — macOS nanosleep run loop
 *   3. main()                  — calls app_init() then loops on app_run()
 *
 * Everything else (pool table, module table, task table, message table,
 * framework init sequence) lives in src/product/app/app.cpp and is shared
 * with the ESP32-IDF target.
 */

#include "pal_logger.h"
#include "module_sim_bridge.h"
#include "app.h"

#include <time.h>
#include <unistd.h>

// ── Platform hook (overrides weak default in app.cpp) ────────────────────────
//
// Called by app_init() before any framework init.
// Responsibilities:
//   1. Set working directory so SPIFFS emulation finds its files.
//   2. Initialise the logger.
//   3. Start the TCP bridge server so it is ready before post_init fires.
//   4. Register ModuleSimBridge as an extra module + task.

extern "C" void app_platform_pre_init(void)
{
#ifdef SIMULATOR_SOURCE_DIR
    if (chdir(SIMULATOR_SOURCE_DIR) != 0) {
        // Logger not yet ready — use printf as fallback
        printf("[SIM] WARNING: chdir to " SIMULATOR_SOURCE_DIR " failed\n");
    }
#endif

    pal_logger_init();

    ModuleSimBridge::instance()->start_server(9000);

    static const hsys_task_desc_t sim_bridge_task = {
        "sim_brdg_t", 4096, 2, 0, { SIM_BRIDGE_MODULE_ID, 0 }
    };
    app_register_extra_module(ModuleSimBridge::instance(), &sim_bridge_task);
}

// ── Run loop (overrides weak default in app.cpp) ─────────────────────────────
//
// On macOS the FreeRTOS-equivalent pthreads keep running independently.
// The main thread sleeps at low cost to avoid burning CPU.

extern "C" void app_run(void)
{
    struct timespec ts = { 0, 10000000 };   // 10 ms
    nanosleep(&ts, nullptr);
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

