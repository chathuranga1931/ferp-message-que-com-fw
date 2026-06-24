// pal_crash_log.h
//
// Platform abstraction for saving full crash/panic information across reboots.
//
// Architecture
// ─────────────
// On ESP32 the linker --wrap intercepts esp_panic_handler before the IDF's
// default implementation runs.  Our wrapper writes the exact-moment uptime and
// a decoded backtrace string into RTC SRAM (survives warm reboot), then calls
// the real handler so the full Guru Meditation output still prints to UART
// unchanged.  After the reboot, the caller reads the RTC data, writes it to SD,
// and sends it to the cloud.
//
// The periodic tick (pal_crash_log_tick) keeps epoch_sec and heap_free current
// so those fields are at most 1 second stale at crash time.
//
// Usage
// ─────
// 1. Call pal_crash_log_init() once, early in app_init() (before RTOS tasks).
// 2. Call pal_crash_log_tick(uptime_ms, epoch_sec) every 1 s from the tick handler.
// 3. On SD mount: if pal_crash_log_pending(), read crash info + backtrace and
//    write them to /panic/crash.json, then call pal_crash_log_clear().
// 4. When connected: send the SD crash file to cloud and delete on success.

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Crash info snapshot ──────────────────────────────────────────────────────

typedef struct {
    uint32_t uptime_ms;   ///< Uptime at exact panic moment (ms)
    uint32_t epoch_sec;   ///< UTC epoch at last tick (0 if NTP not synced)
    uint32_t heap_free;   ///< Free heap at last tick (bytes)
} pal_crash_info_t;

// ── API ──────────────────────────────────────────────────────────────────────

/// Call once at startup, before any RTOS tasks.
void pal_crash_log_init(void);

/// Call every 1 s to keep epoch_sec and heap_free current.
void pal_crash_log_tick(uint32_t uptime_ms, uint32_t epoch_sec);

/// Returns true if the previous reboot was a panic or watchdog crash.
bool pal_crash_log_pending(void);

/// Fill *out with crash info from previous boot.
/// Valid only while pal_crash_log_pending() returns true.
void pal_crash_log_get(pal_crash_info_t *out);

/// Returns the decoded backtrace string from the previous crash.
/// Format: "0xPC:0xSP 0xPC:0xSP ..."
/// Returns empty string if no backtrace was captured.
/// Valid only while pal_crash_log_pending() returns true.
const char *pal_crash_log_get_backtrace(void);

/// Clear crash state. Call after persisting crash info to SD/cloud.
void pal_crash_log_clear(void);

#ifdef __cplusplus
}
#endif
