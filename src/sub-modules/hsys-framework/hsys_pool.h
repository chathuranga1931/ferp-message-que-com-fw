// hsys_pool.h
//
// Buffer pool API.
//
// All message payload data must be allocated from the pool before publishing.
// The pool uses pre-allocated static memory — no heap allocation at runtime.
//
// Size classes and block counts are supplied by the caller as a const table
// at init time (typically defined in main.c), so no pool configuration lives
// inside the library itself.

#ifndef HSYS_POOL_H
#define HSYS_POOL_H

#include "hsys_types.h"
#include "hsys_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Pool class configuration  (supplied by the application)
// ---------------------------------------------------------------------------

/**
 * @brief  One entry in the pool class table.
 *         Entries must be listed in ascending block_size order.
 */
typedef struct {
    uint16_t  block_size;    ///< Size of each buffer in this class (bytes)
    uint16_t  block_count;   ///< Number of buffers to pre-allocate
} hsys_pool_class_cfg_t;

// ---------------------------------------------------------------------------
// Pool diagnostics (read-only snapshot per size class)
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t  block_size;     ///< Size of each buffer in bytes
    uint16_t  total_count;    ///< Total blocks in this class
    uint16_t  free_count;     ///< Currently available blocks
    uint16_t  peak_used;      ///< Peak simultaneous allocations (watermark)
} hsys_pool_class_info_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the pool manager with an application-supplied class table.
 *
 *         The table and the backing arena must remain valid for the lifetime
 *         of the firmware.  Call before hsys_msg_init() or any publish.
 *
 * @param  classes      Pointer to an array of class configuration entries.
 *                      Entries must be in ascending block_size order.
 * @param  class_count  Number of entries in the array.
 * @return HSYS_OK, HSYS_ERR_INVALID, or HSYS_ERR_NO_MEM.
 */
hsys_status_t hsys_pool_init(const hsys_pool_class_cfg_t *classes,
                              uint8_t class_count);

/**
 * @brief  Allocate a buffer of at least `size` bytes from the pool.
 *         Returns the smallest available block that fits.
 *         Thread-safe and ISR-safe.
 *
 * @param  size  Required payload size in bytes.
 * @return Pointer to a buffer, or NULL if no suitable block is available.
 */
void *hsys_pool_alloc(uint16_t size);

/**
 * @brief  Return a buffer to the pool.
 *         The pointer must have been obtained from hsys_pool_alloc().
 *         Thread-safe and ISR-safe.
 *
 * @param  ptr  Buffer to release. Silently ignored if NULL.
 */
void hsys_pool_free(void *ptr);

/**
 * @brief  Get diagnostic information for a pool size class by index.
 *
 * @param  class_index  Index into the pool class table (0 … class_count-1).
 * @param  info         Output — filled with size and count information.
 * @return HSYS_OK or HSYS_ERR_INVALID if index is out of range.
 */
hsys_status_t hsys_pool_get_info(uint8_t class_index,
                                  hsys_pool_class_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // HSYS_POOL_H