#include "sim_init.h"
#include "pal_logger.h"
#include "module_sim_bridge.h"

#include <cstdio>
#include <time.h>

int main()
{
    pal_logger_init();

    pal_logger_log(true, "[simulator] starting ferp-com-simulator\n");

    // Start the TCP server BEFORE sim_app_init() so ModuleSimBridge is
    // ready before any messages begin to flow.
    ModuleSimBridge::instance()->start_server(9000);

    sim_app_init();

    pal_logger_log(true, "[simulator] app initialised — entering run loop\n");

    while (true) {
        // macOS: tasks are pthreads; main thread spins at low cost
        struct timespec ts = { 0, 10000000 }; // 10 ms
        nanosleep(&ts, nullptr);
    }

    return 0;
}
