// app_hw_config.h
//
// Board-level constants for the FERP USB printer (ESP32-S3, USB OTG host).
// The printer is attached to the S3 native USB port (GPIO19 / GPIO20); the
// USB host library drives it, no pins are configured here.

#pragma once
#include <stdint.h>

// No buzzer / button defined for this board yet.  To add a buzzer:
//   #define PRINTER_BUZZER_GPIO  <gpio>

// ── Product identity (used by app.cpp) ──────────────────────────────────────
#define PRINTER_TRANSPORT_USB    1                    ///< ESC/POS over USB bulk OUT
#define PRINTER_MQTT_DEV_TYPE    "ferp-printer"       ///< ferp/<dev_type>/<group>/<mac>/...
#define PRINTER_OTA_TARGET       "printer-usb-main"   ///< OTA bundle target name for this app image
#define PRINTER_HTTP_PORT        80                   ///< legacy printer API port
