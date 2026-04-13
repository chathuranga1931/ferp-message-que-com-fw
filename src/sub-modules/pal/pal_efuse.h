#ifndef PAL_EFUSE_H
#define PAL_EFUSE_H

#include <stdint.h>
#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get default MAC address from hardware
 * 
 * Reads the factory-programmed MAC address from the device's eFuse/OTP memory.
 * This is typically the WiFi station MAC address.
 * 
 * @param mac Pointer to buffer to store MAC address (must be at least 6 bytes)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_efuse_get_mac(uint8_t* mac, size_t length);

/**
 * @brief Get unique chip ID
 * 
 * Reads a unique identifier for the chip from eFuse/OTP memory.
 * The length and format may vary by platform.
 * 
 * @param id Pointer to buffer to store chip ID
 * @param length Pointer to variable containing buffer size (input) and actual ID length (output)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_efuse_get_chip_id(uint8_t* id, size_t* length);

#ifdef __cplusplus
}
#endif

#endif // PAL_EFUSE_H
