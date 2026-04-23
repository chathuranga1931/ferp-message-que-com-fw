/**
 * @file pal_mac_i2c.cpp
 * @brief PAL I2C implementation for the macOS simulator.
 *
 * Instead of real hardware, all I2C traffic is routed to registered
 * pal_i2c_emulator_t callback tables.  Devices are registered via
 * pal_mac_i2c_register_device() before the HSYS tasks start.
 *
 * At most PAL_MAC_I2C_MAX_DEVICES per port are supported.
 */

#include "pal_i2c.h"
#include "pal_mac_i2c_emulator.h"
#include "pal_logger.h"

#define __TAG__  "PAL_I2C "
#define I2C_LOG  false

// ---------------------------------------------------------------------------
// Device registry
// ---------------------------------------------------------------------------

#define PAL_MAC_I2C_MAX_DEVICES  8u

typedef struct {
    uint8_t             port;
    uint8_t             addr;
    pal_i2c_emulator_t *emulator;
} i2c_entry_t;

static i2c_entry_t s_devices[PAL_MAC_I2C_MAX_DEVICES];
static uint32_t    s_device_count = 0u;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static pal_i2c_emulator_t *find_emulator(uint8_t port, uint8_t addr)
{
    for (uint32_t i = 0u; i < s_device_count; i++) {
        if (s_devices[i].port == port && s_devices[i].addr == addr) {
            return s_devices[i].emulator;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Registration API  (declared in pal_mac_i2c_emulator.h)
// ---------------------------------------------------------------------------

extern "C" void pal_mac_i2c_register_device(uint8_t port, uint8_t addr,
                                             pal_i2c_emulator_t *emulator)
{
    if (!emulator || s_device_count >= PAL_MAC_I2C_MAX_DEVICES) {
        LOG_MSG_ERROR(I2C_LOG, "register_device: table full or null emulator");
        return;
    }
    s_devices[s_device_count].port     = port;
    s_devices[s_device_count].addr     = addr;
    s_devices[s_device_count].emulator = emulator;
    s_device_count++;
    LOG_MSG_INFO(I2C_LOG, "Registered I2C device port=%u addr=0x%02X", port, addr);
}

// ---------------------------------------------------------------------------
// pal_i2c interface
// ---------------------------------------------------------------------------

extern "C" int32_t pal_i2c_init(pal_i2c_port_t port, const pal_i2c_config_t *config)
{
    (void)port; (void)config;
    return PAL_OK;
}

extern "C" int32_t pal_i2c_deinit(pal_i2c_port_t port)
{
    (void)port;
    return PAL_OK;
}

extern "C" bool pal_i2c_device_probe(pal_i2c_port_t port, uint8_t device_address,
                                      uint32_t timeout_ms)
{
    (void)timeout_ms;
    return (find_emulator((uint8_t)port, device_address) != NULL);
}

extern "C" int32_t pal_i2c_write(pal_i2c_port_t port, uint8_t device_address,
                                  const uint8_t *data, size_t length,
                                  uint32_t timeout_ms)
{
    (void)timeout_ms;
    pal_i2c_emulator_t *em = find_emulator((uint8_t)port, device_address);
    if (!em) {
        LOG_MSG_WARNING(I2C_LOG, "write: no emulator for port=%u addr=0x%02X",
                        (unsigned)port, device_address);
        return PAL_ERROR_NOT_FOUND;
    }
    if (!em->on_write) return PAL_ERROR;
    return em->on_write(data, length, em->ctx);
}

extern "C" int32_t pal_i2c_read(pal_i2c_port_t port, uint8_t device_address,
                                 uint8_t *data, size_t length,
                                 uint32_t timeout_ms)
{
    (void)timeout_ms;
    pal_i2c_emulator_t *em = find_emulator((uint8_t)port, device_address);
    if (!em) {
        LOG_MSG_WARNING(I2C_LOG, "read: no emulator for port=%u addr=0x%02X",
                        (unsigned)port, device_address);
        return PAL_ERROR_NOT_FOUND;
    }
    if (!em->on_read) return PAL_ERROR;
    return em->on_read(data, length, em->ctx);
}

extern "C" int32_t pal_i2c_write_read(pal_i2c_port_t port, uint8_t device_address,
                                       const uint8_t *write_data, size_t write_length,
                                       uint8_t *read_data, size_t read_length,
                                       uint32_t timeout_ms)
{
    (void)timeout_ms;
    pal_i2c_emulator_t *em = find_emulator((uint8_t)port, device_address);
    if (!em) {
        LOG_MSG_WARNING(I2C_LOG, "write_read: no emulator for port=%u addr=0x%02X",
                        (unsigned)port, device_address);
        return PAL_ERROR_NOT_FOUND;
    }
    if (!em->on_write_read) return PAL_ERROR;
    return em->on_write_read(write_data, write_length, read_data, read_length, em->ctx);
}
