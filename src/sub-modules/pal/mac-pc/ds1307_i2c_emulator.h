// ds1307_i2c_emulator.h
//
// macOS I2C emulator for the DS1307 real-time clock.
//
// Implements the DS1307 register-bank protocol over the pal_i2c_emulator_t
// callback interface.  Uses gettimeofday() as the time source so the simulated
// RTC always reflects the host clock.
//
// A signed offset (seconds) is maintained so that ds1307_set_time() calls
// shift subsequent reads without touching the host system clock.

#pragma once

#include "pal_mac_i2c_emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the DS1307 I2C emulator state (call once at startup).
 */
void ds1307_i2c_emulator_init(void);

/**
 * @brief Return a pointer to the emulator callback table.
 *
 * Pass this to pal_mac_i2c_register_device() during app_platform_pre_init().
 */
pal_i2c_emulator_t *ds1307_i2c_emulator_get(void);

#ifdef __cplusplus
}
#endif
