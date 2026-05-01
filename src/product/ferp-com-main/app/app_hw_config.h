// app_hw_config.h
//
// Compile-time hardware constants shared across application modules.
// Physical I2C addresses, pin numbers, and other board-level parameters go here.

#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// I2C device addresses (fixed by chip specification)
// ---------------------------------------------------------------------------
#define APP_HW_I2C_ADDR_DS1307   ((uint8_t)0x68)   ///< DS1307 RTC — address is hardwired

// ---------------------------------------------------------------------------
// I2C port assignment
// ---------------------------------------------------------------------------
#define APP_HW_I2C_PORT_RTC      PAL_I2C_PORT_0    ///< I2C port used for the RTC
