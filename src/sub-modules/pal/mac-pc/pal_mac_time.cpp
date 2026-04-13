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
