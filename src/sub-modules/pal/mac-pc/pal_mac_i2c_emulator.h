// pal_mac_i2c_emulator.h
//
// Emulator plug-in interface for the macOS I2C PAL.
//
// Any "device" that should respond to I2C traffic in the simulator registers
// an instance of pal_i2c_emulator_t together with its bus/address pair via
// pal_mac_i2c_register_device().  From that point on, every pal_i2c_write /
// pal_i2c_write_read call for that address is routed to the callbacks.
//
// Registration must happen before the HSYS tasks start (i.e. inside
// app_platform_pre_init()).

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Emulator callback interface
// ---------------------------------------------------------------------------

/**
 * @brief Callback table for a single emulated I2C device.
 *
 * Each function should return 0 (PAL_OK) on success or a negative value on
 * failure.  Unused callbacks may be set to NULL — the PAL will return an
 * error for the corresponding operation in that case.
 */
typedef struct {
    /** Called when pal_i2c_write() targets this device. */
    int32_t (*on_write)(const uint8_t *data, size_t len, void *ctx);

    /** Called when pal_i2c_write_read() targets this device. */
    int32_t (*on_write_read)(const uint8_t *wr,  size_t wr_len,
                              uint8_t       *rd,  size_t rd_len,
                              void          *ctx);

    /** Called when pal_i2c_read() targets this device (optional). */
    int32_t (*on_read)(uint8_t *rd, size_t rd_len, void *ctx);

    /** Opaque pointer forwarded to every callback. */
    void *ctx;
} pal_i2c_emulator_t;

// ---------------------------------------------------------------------------
// Registration API
// ---------------------------------------------------------------------------

/**
 * @brief Register an I2C device emulator.
 *
 * @param port     PAL I2C port (e.g. PAL_I2C_PORT_0).
 * @param addr     7-bit I2C device address.
 * @param emulator Pointer to the emulator callback table (must remain valid
 *                 for the lifetime of the program).
 */
void pal_mac_i2c_register_device(uint8_t port, uint8_t addr,
                                  pal_i2c_emulator_t *emulator);

#ifdef __cplusplus
}
#endif
