/**
 * @file pal_esp_idf_time.cpp
 * @brief ESP-IDF implementation of time PAL
 */

#include "pal_time.h"

#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "PAL_TIME";

// ============================================================================
// Time Retrieval Functions
// ============================================================================

uint64_t pal_time_get_ms(void) {
    // esp_timer_get_time() returns microseconds
    return esp_timer_get_time() / 1000ULL;
}

uint64_t pal_time_get_us(void) {
    return esp_timer_get_time();
}

uint64_t pal_time_get_us_from_isr(void) {
    // esp_timer_get_time() reads directly from a hardware register —
    // it does not take any FreeRTOS locks and is safe to call from an ISR.
    return esp_timer_get_time();
}

uint64_t pal_time_get_sec(void) {
    return esp_timer_get_time() / 1000000ULL;
}

// ============================================================================
// Delay Functions
// ============================================================================

void pal_time_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void pal_time_delay_us(uint32_t us) {
    // For microsecond delays, use esp_rom_delay_us for precise timing
    // Note: This is a busy-wait delay
    if (us > 0) {
        uint64_t start = esp_timer_get_time();
        while ((esp_timer_get_time() - start) < us) {
            // Busy wait
        }
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

bool pal_time_is_timeout(uint64_t start_ms, uint32_t timeout_ms) {
    uint64_t current_ms = pal_time_get_ms();
    
    // Handle wrap-around (though unlikely with 64-bit counter)
    if (current_ms < start_ms) {
        // Wrap-around occurred
        uint64_t elapsed = (UINT64_MAX - start_ms) + current_ms + 1;
        return (elapsed >= timeout_ms);
    }
    
    return ((current_ms - start_ms) >= timeout_ms);
}

uint64_t pal_time_elapsed_ms(uint64_t start_ms) {
    uint64_t current_ms = pal_time_get_ms();
    
    // Handle wrap-around (though unlikely with 64-bit counter)
    if (current_ms < start_ms) {
        // Wrap-around occurred
        return (UINT64_MAX - start_ms) + current_ms + 1;
    }
    
    return (current_ms - start_ms);
}

// ============================================================================
// System Time Functions
// ============================================================================

int32_t pal_time_get_epoch_time(time_t* epoch_time) {
    if (epoch_time == NULL) {
        return -1;
    }
    
    // Get current system time (Unix epoch time)
    time(epoch_time);
    
    // Validate time - if time is less than 2020-01-01, it means NTP hasn't synced yet
    // Unix timestamp for 2020-01-01 00:00:00 UTC is 1577836800
    if (*epoch_time < 1577836800) {
        return -1;  // Time not synchronized yet
    }
    
    return 0;  // Success
}

int32_t pal_time_set_epoch_time(time_t epoch_time) {
    // Validate input - must be a reasonable time (after 2020-01-01)
    if (epoch_time < 1577836800) {
        return -1;  // Invalid time
    }
    
    struct timeval tv;
    tv.tv_sec = epoch_time;
    tv.tv_usec = 0;
    
    if (settimeofday(&tv, NULL) < 0) {
        return -1;  // Failed to set time
    }
    
    return 0;  // Success
}
