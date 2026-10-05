/*
 * main.cpp — FERP receipt printer (USB model, ESP32-S3) entry point.
 *
 * All wiring lives in ../../app/app.cpp; this file only starts it.
 */

#include "app.h"
#include "pal/pal_crash_log.h"  // pal_crash_log_disable_boot_wdt

extern "C" void app_main(void)
{
    // See ferp-com-v3 main.cpp: stop the RTC WDT armed by esp_restart_noos()
    // before the (slow) boot path can let it wipe the crash data in RTC SRAM.
    pal_crash_log_disable_boot_wdt();

    app_init();

    while (true) {
        app_run();
    }
}
