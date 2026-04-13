#ifndef PAL_I2C_H
#define PAL_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C port identifier
 */
typedef enum {
    PAL_I2C_PORT_0 = 0,
    PAL_I2C_PORT_1 = 1,
} pal_i2c_port_t;

/**
 * @brief I2C mode
 */
typedef enum {
    PAL_I2C_MODE_MASTER = 0,
    PAL_I2C_MODE_SLAVE = 1,
} pal_i2c_mode_t;

/**
 * @brief I2C configuration structure
 */
typedef struct {
    pal_i2c_mode_t mode;           // Master or slave mode
    int32_t sda_pin;               // SDA GPIO pin
    int32_t scl_pin;               // SCL GPIO pin
    bool sda_pullup_enable;        // Enable internal pullup on SDA
    bool scl_pullup_enable;        // Enable internal pullup on SCL
    uint32_t clock_speed;          // Clock frequency in Hz (e.g., 100000 for 100kHz)
} pal_i2c_config_t;

/**
 * @brief Initialize I2C port
 * 
 * @param port I2C port number
 * @param config I2C configuration
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_i2c_init(pal_i2c_port_t port, const pal_i2c_config_t* config);

/**
 * @brief Deinitialize I2C port
 * 
 * @param port I2C port number
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_i2c_deinit(pal_i2c_port_t port);

/**
 * @brief Check if an I2C device is present on the bus
 * 
 * @param port I2C port number
 * @param device_address 7-bit I2C device address
 * @param timeout_ms Timeout in milliseconds
 * @return true if device responds, false otherwise
 */
bool pal_i2c_device_probe(pal_i2c_port_t port, uint8_t device_address, uint32_t timeout_ms);

/**
 * @brief Write data to I2C device
 * 
 * @param port I2C port number
 * @param device_address 7-bit I2C device address
 * @param data Pointer to data buffer
 * @param length Number of bytes to write
 * @param timeout_ms Timeout in milliseconds
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_i2c_write(pal_i2c_port_t port, uint8_t device_address, 
                      const uint8_t* data, size_t length, uint32_t timeout_ms);

/**
 * @brief Read data from I2C device
 * 
 * @param port I2C port number
 * @param device_address 7-bit I2C device address
 * @param data Pointer to buffer to store read data
 * @param length Number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_i2c_read(pal_i2c_port_t port, uint8_t device_address, 
                     uint8_t* data, size_t length, uint32_t timeout_ms);

/**
 * @brief Write then read from I2C device (write register address, then read data)
 * 
 * @param port I2C port number
 * @param device_address 7-bit I2C device address
 * @param write_data Pointer to data to write (usually register address)
 * @param write_length Number of bytes to write
 * @param read_data Pointer to buffer to store read data
 * @param read_length Number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_i2c_write_read(pal_i2c_port_t port, uint8_t device_address,
                           const uint8_t* write_data, size_t write_length,
                           uint8_t* read_data, size_t read_length,
                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // PAL_I2C_H
