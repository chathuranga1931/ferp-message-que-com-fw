/**
 * @file pal_mac_time.cpp
 * @brief macOS / Linux implementation of pal_time.h
 *
 * Uses CLOCK_MONOTONIC for a stable, boot-relative millisecond / microsecond
 * counter that is unaffected by NTP adjustments or wall-clock changes.
 */

#include "pal_time.h"

#include <time.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Boot-relative offset — captured on first call so t=0 is program start
// ---------------------------------------------------------------------------

static uint64_t s_boot_ms = 0;
static uint64_t s_boot_us = 0;

static void capture_boot_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s_boot_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
    s_boot_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
}

// ---------------------------------------------------------------------------
// pal_time API
// ---------------------------------------------------------------------------

uint64_t pal_time_get_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
    if (s_boot_ms == 0) { s_boot_ms = now; s_boot_us = now * 1000ULL; }
    return now - s_boot_ms;
}

uint64_t pal_time_get_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
    if (s_boot_us == 0) { s_boot_us = now; s_boot_ms = now / 1000ULL; }
    return now - s_boot_us;
}

// On macOS there is no actual ISR context — but this must return absolute
// CLOCK_MONOTONIC microseconds (NOT relative to program start) so that
// timestamp_us / 1000 == get_current_time_ms() inside hsys_button.cpp.
// Both use the same CLOCK_MONOTONIC base → elapsed-time arithmetic is correct.
uint64_t pal_time_get_us_from_isr(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
}

uint64_t pal_time_get_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec;
}

bool pal_time_is_timeout(uint64_t start_ms, uint32_t timeout_ms)
{
    return (pal_time_get_ms() - start_ms) >= (uint64_t)timeout_ms;
}

uint64_t pal_time_elapsed_ms(uint64_t start_ms)
{
    return pal_time_get_ms() - start_ms;
}

int32_t pal_time_get_epoch_time(time_t *epoch_time)
{
    if (epoch_time == nullptr) return -1;
    *epoch_time = time(nullptr);
    return 0;
}

int32_t pal_time_set_epoch_time(time_t /*epoch_time*/)
{
    // macOS: setting system time requires root — silently ignore in simulator
    return 0;
}

void pal_time_delay_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

void pal_time_delay_us(uint32_t us)
{
    struct timespec ts;
    ts.tv_sec  = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000L;
    nanosleep(&ts, nullptr);
}
