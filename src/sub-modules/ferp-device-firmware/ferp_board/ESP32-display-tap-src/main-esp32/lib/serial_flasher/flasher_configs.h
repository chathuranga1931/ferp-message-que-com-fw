#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIGHER_BAUDRATE 115200
#define SERIAL_FLASHER_BOOT_HOLD_TIME_MS 50
#define SERIAL_FLASHER_RESET_HOLD_TIME_MS 100
#define SERIAL_FLASHER_INTERFACE_UART 1
#define SERIAL_FLASHER_WRITE_BLOCK_RETRIES 3
// #define SINGLE_TARGET_SUPPORT 1
#define SERIAL_FLASHER_INTERFACE SERIAL_FLASHER_INTERFACE_UART
#define MD5_ENABLED 1

#ifdef __cplusplus
}
#endif