#ifndef CRC32_HPP
#define CRC32_HPP

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Update CRC32 with new data.
 *
 * @param crc     Previous CRC32 value (use 0xFFFFFFFF for first call)
 * @param data    Pointer to data buffer
 * @param length  Number of bytes
 * @return uint32_t Updated CRC32 value
 *
 * @note Final CRC should be XORed with 0xFFFFFFFF if you want the standard result.
 */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // CRC32_HPP
