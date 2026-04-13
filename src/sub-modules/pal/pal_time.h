/**
 * @file pal_time.h
 * @brief Platform Abstraction Layer for time operations
 * 
 * This header provides a platform-independent interface for time-related
 * functions including millisecond counters, microsecond timing, and delays.
 */

#ifndef PAL_TIME_H
#define PAL_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Time Retrieval Functions
// ============================================================================

/**
 * @brief Get milliseconds since boot
 * 
 * Returns the number of milliseconds since the system was powered on or reset.
 * This counter will wrap around approximately every 49.7 days.
 * 
 * @return uint64_t Number of milliseconds since boot
 */
uint64_t pal_time_get_ms(void);

/**
 * @brief Get microseconds since boot
 * 
 * Returns the number of microseconds since the system was powered on or reset.
 * Provides higher resolution timing than pal_time_get_ms().
 * 
 * @return uint64_t Number of microseconds since boot
 */
uint64_t pal_time_get_us(void);

/**
 * @brief Get microseconds since boot — ISR-safe version
 *
 * Identical to pal_time_get_us() but explicitly documented as safe to call
 * from an interrupt service routine (ISR). Use this instead of pal_time_get_us()
 * inside any IRAM_ATTR / ISR context to make the intent clear.
 *
 * @return uint64_t Number of microseconds since boot
 */
uint64_t pal_time_get_us_from_isr(void);

/**
 * @brief Get seconds since boot
 * 
 * Returns the number of seconds since the system was powered on or reset.
 * 
 * @return uint64_t Number of seconds since boot
 */
uint64_t pal_time_get_sec(void);

// ============================================================================
// Delay Functions
// ============================================================================

/**
 * @brief Delay for specified milliseconds
 * 
 * Blocks execution for the specified number of milliseconds.
 * This is a busy-wait delay and should be used sparingly.
 * For longer delays, consider using RTOS sleep functions.
 * 
 * @param ms Number of milliseconds to delay
 */
void pal_time_delay_ms(uint32_t ms);

/**
 * @brief Delay for specified microseconds
 * 
 * Blocks execution for the specified number of microseconds.
 * This is a busy-wait delay for precise short delays.
 * 
 * @param us Number of microseconds to delay
 */
void pal_time_delay_us(uint32_t us);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if a timeout has occurred
 * 
 * Helper function to check if a specified timeout period has elapsed
 * since a starting timestamp.
 * 
 * @param start_ms Starting timestamp in milliseconds (from pal_time_get_ms)
 * @param timeout_ms Timeout period in milliseconds
 * @return bool true if timeout has occurred, false otherwise
 */
bool pal_time_is_timeout(uint64_t start_ms, uint32_t timeout_ms);

/**
 * @brief Calculate elapsed time
 * 
 * Calculate the number of milliseconds elapsed since a starting timestamp.
 * Handles wrap-around correctly.
 * 
 * @param start_ms Starting timestamp in milliseconds (from pal_time_get_ms)
 * @return uint64_t Number of milliseconds elapsed
 */
uint64_t pal_time_elapsed_ms(uint64_t start_ms);

// ============================================================================
// System Time Functions
// ============================================================================

/**
 * @brief Get current system epoch time
 * 
 * Returns the current Unix epoch time (seconds since January 1, 1970 00:00:00 UTC).
 * This time is synchronized with NTP if available, otherwise it starts from boot time.
 * 
 * @param epoch_time Pointer to store the epoch time (in seconds)
 * @return int32_t 0 on success, -1 if time is not valid (not synced yet)
 */
int32_t pal_time_get_epoch_time(time_t* epoch_time);

/**
 * @brief Set system epoch time
 * 
 * Sets the system time to the specified Unix epoch time.
 * This function is typically used to initialize system time from an RTC or external source.
 * 
 * @param epoch_time The epoch time to set (in seconds since 1970-01-01 00:00:00 UTC)
 * @return int32_t 0 on success, -1 on failure
 */
int32_t pal_time_set_epoch_time(time_t epoch_time);

#ifdef __cplusplus
}
#endif

#endif /* PAL_TIME_H */
