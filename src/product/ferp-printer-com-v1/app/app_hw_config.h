// app_hw_config.h
//
// Board-level constants for the FERP COM printer (board 2308, ESP32).
// Pins match old/ferp-printer/src/bsp/printer-boards/printer_2308.h.

#pragma once
#include <stdint.h>

// ESC/POS printer on UART2
#define PRINTER_UART_PORT        PAL_UART_PORT_2
#define PRINTER_UART_TX_GPIO     17
#define PRINTER_UART_RX_GPIO     16

// Buzzer (active high) — beeps when a print fails
#define PRINTER_BUZZER_GPIO      12

// Print button (GPIO13) exists on the board but had no function in the
// legacy firmware; it is not used.
#define PRINTER_BTN_GPIO         13

// ── Product identity (used by app.cpp) ──────────────────────────────────────
#define PRINTER_TRANSPORT_UART   1                    ///< ESC/POS over UART
#define PRINTER_MQTT_DEV_TYPE    "ferp-printer"       ///< ferp/<dev_type>/<group>/<mac>/...
#define PRINTER_OTA_TARGET       "printer-com-main"   ///< OTA bundle target name for this app image
#define PRINTER_HTTP_PORT        80                   ///< legacy printer API port
