#include "sim_init.h"
#include "pal_logger.h"
#include "module_sim_bridge.h"

#define __TAG__ "SIM_MAIN"
#ifndef SIM_MAIN_LOG_EN
#define SIM_MAIN_LOG_EN true
#endif

#include <cstdio>
#include <time.h>

int main()
{
    pal_logger_init();

    LOG_MSG_INFO(SIM_MAIN_LOG_EN, "starting ferp-com-simulator");

    // Start the TCP server BEFORE sim_app_init() so ModuleSimBridge is
    // ready before any messages begin to flow.
    ModuleSimBridge::instance()->start_server(9000);

    sim_app_init();

    LOG_MSG_INFO(SIM_MAIN_LOG_EN, "app initialised — entering run loop");

    while (true) {
        // macOS: tasks are pthreads; main thread spins at low cost
        struct timespec ts = { 0, 10000000 }; // 10 ms
        nanosleep(&ts, nullptr);
    }

    return 0;
}
