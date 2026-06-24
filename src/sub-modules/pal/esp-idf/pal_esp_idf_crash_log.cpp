// pal_esp_idf_crash_log.cpp
//
// ESP-IDF implementation of the crash-log PAL.
//
// Mechanism
// ──────────
// The linker --wrap=esp_panic_handler flag (set in the main CMakeLists.txt)
// redirects every call to esp_panic_handler() through our wrapper
// __wrap_esp_panic_handler() without modifying any IDF source file.
//
// Our wrapper:
//   1. Writes exact-moment uptime (from hardware timer) and decoded backtrace
//      string into RTC SRAM — memory that survives a warm reboot.
//   2. Hands off to __real_esp_panic_handler() which prints the full Guru
//      Meditation Error + register dump + backtrace to UART and then resets,
//      exactly as the unmodified IDF would.
//
// After the reboot, pal_crash_log_init() detects the crash via
// esp_reset_reason(), and the RTC data is available for writing to SD and
// sending to the cloud.
//
// epoch_sec and heap_free are maintained by the periodic tick (called every 1 s)
// so they are at most 1 second stale at crash time.

#include "pal_crash_log.h"

// IDF panic internals — available because esp_system is in PRIV_REQUIRES
#include "esp_private/panic_internal.h"   // panic_info_t, panic_print_*
// IDF system headers
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
// Debug helpers — esp_backtrace_frame_t, esp_backtrace_get_next_frame
#include <esp_debug_helpers.h>

// ── RTC SRAM layout ──────────────────────────────────────────────────────────

#define CRASH_MAGIC          0xC5A5C5A5UL
#define CRASH_BACKTRACE_SIZE 512   ///< Max chars for "0xPC:0xSP " pairs

typedef struct {
    uint32_t magic;
    uint32_t uptime_ms;
    uint32_t epoch_sec;   ///< from last tick
    uint32_t heap_free;   ///< from last tick
    char     backtrace[CRASH_BACKTRACE_SIZE];
} crash_rtc_t;

static RTC_DATA_ATTR crash_rtc_t s_rtc;

static bool             s_pending = false;
static pal_crash_info_t s_info    = {};

// ── Xtensa exception frame (ABI-stable for ESP32/S2/S3) ──────────────────────
// Matches XtExcFrame in xtensa_context.h — field offsets are guaranteed by the
// Xtensa ISA ABI, not by IDF versioning.

typedef struct {
    uint32_t exit;                          // 0
    uint32_t pc;                            // 4
    uint32_t ps;                            // 8
    uint32_t a0;   uint32_t a1;             // 12, 16  (a0=retaddr a1=sp)
    uint32_t a2;   uint32_t a3;             // 20, 24
    uint32_t a4;   uint32_t a5;             // 28, 32
    uint32_t a6;   uint32_t a7;             // 36, 40
    uint32_t a8;   uint32_t a9;             // 44, 48
    uint32_t a10;  uint32_t a11;            // 52, 56
    uint32_t a12;  uint32_t a13;            // 60, 64
    uint32_t a14;  uint32_t a15;            // 68, 72
    uint32_t sar;                           // 76
    uint32_t exccause;                      // 80
    uint32_t excvaddr;                      // 84
} crash_frame_t;

// ── panic_info_t compatible struct (verified against IDF 6.0.1 layout) ───────
// panic_internal.h field order: core, exception(enum=int), reason, description,
// details(fn ptr), state(fn ptr), addr, frame, pseudo_excause.

typedef void (*crash_dump_fn_t)(const void *frame);

typedef struct {
    int             core;
    int             exception;       // panic_exception_t enum, underlying int
    const char     *reason;
    const char     *description;
    crash_dump_fn_t details;
    crash_dump_fn_t state;
    const void     *addr;
    const void     *frame;           // points to crash_frame_t
    bool            pseudo_excause;
} crash_info_t;

// ── Forward declarations ──────────────────────────────────────────────────────

// Created automatically by the linker --wrap mechanism.
// The linker replaces esp_panic_handler → __wrap_esp_panic_handler and makes
// __real_esp_panic_handler available as the original IDF implementation.
extern "C" void __attribute__((noreturn)) __real_esp_panic_handler(crash_info_t *info);

// ── PC processing for Xtensa windowed ABI ────────────────────────────────────
// Equivalent to esp_cpu_process_stack_pc() from xtensa/include/esp_cpu_utils.h.
// Inlined here to avoid a dependency on a component-private include path.

static inline uint32_t _process_pc(uint32_t pc)
{
    if (pc & 0x80000000) {
        // Top two bits of a0 indicate the window increment — map to address space
        pc = (pc & 0x3fffffff) | 0x40000000;
    }
    return pc - 3;  // subtract 3 to get the calling instruction's address
}

// ── Manual hex formatting (no heap, no libc) ─────────────────────────────────

static void _append_hex32(char *buf, size_t buf_size, size_t *pos, uint32_t val)
{
    static const char hex[] = "0123456789abcdef";
    if (*pos + 10 >= buf_size) return;    // "0x" + 8 hex digits + guard
    buf[(*pos)++] = '0';
    buf[(*pos)++] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[(*pos)++] = hex[(val >> (i * 4)) & 0xF];
    }
}

// ── Backtrace string builder ──────────────────────────────────────────────────

static void _build_backtrace(const crash_frame_t *frame,
                              char *buf, size_t buf_size)
{
    if (!frame || buf_size < 2) { buf[0] = '\0'; return; }

    // Seed the IDF backtrace walker from the exception frame registers.
    // Same initialisation as panic_print_backtrace() in panic_arch.c.
    esp_backtrace_frame_t bt = {
        .pc       = frame->pc,
        .sp       = frame->a1,
        .next_pc  = frame->a0,
        .exc_frame = frame,
    };

    size_t pos = 0;
    for (int depth = 0; depth < 20; depth++) {
        uint32_t pc = _process_pc(bt.pc);
        _append_hex32(buf, buf_size, &pos, pc);
        if (pos < buf_size) buf[pos++] = ':';
        _append_hex32(buf, buf_size, &pos, bt.sp);
        if (pos < buf_size) buf[pos++] = ' ';
        if (!esp_backtrace_get_next_frame(&bt)) break;
    }
    // Trim trailing space and null-terminate
    if (pos > 0 && buf[pos - 1] == ' ') pos--;
    buf[pos < buf_size ? pos : buf_size - 1] = '\0';
}

// ── Panic handler wrapper ─────────────────────────────────────────────────────
//
// The linker --wrap routes every call to esp_panic_handler() here.
// We capture data to RTC, then call the REAL IDF handler which:
//   - prints "Guru Meditation Error: Core N panic'ed (...)"
//   - calls PANIC_INFO_DUMP(info, details)  → exception-specific details
//   - calls PANIC_INFO_DUMP(info, state)    → register dump + backtrace to UART
//   - sets esp_reset_reason hint
//   - calls panic_restart() → device resets
// So the UART output is 100% identical to the unmodified IDF.

extern "C" void __attribute__((noreturn)) __wrap_esp_panic_handler(crash_info_t *info)
{
    const crash_frame_t *frame = (const crash_frame_t *)info->frame;

    // Exact uptime from the hardware high-resolution timer (safe in panic context)
    s_rtc.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Backtrace built from the live exception frame (exact crash point)
    _build_backtrace(frame, s_rtc.backtrace, sizeof(s_rtc.backtrace));

    // epoch_sec and heap_free retain the values set by the last tick call —
    // they are at most 1 second stale, and calling NTP or the heap allocator
    // during panic is not safe.

    // Mark RTC data valid so pal_crash_log_init() recognises it after reboot
    s_rtc.magic = CRASH_MAGIC;

    // Hand off to the original IDF handler — identical UART output, then resets
    __real_esp_panic_handler(info);
}

// ── PAL API implementation ────────────────────────────────────────────────────

extern "C" void pal_crash_log_init(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    bool is_crash = (reason == ESP_RST_PANIC   ||
                     reason == ESP_RST_TASK_WDT ||
                     reason == ESP_RST_INT_WDT  ||
                     reason == ESP_RST_WDT);
    if (!is_crash) return;

    s_pending = true;
    if (s_rtc.magic == CRASH_MAGIC) {
        s_info.uptime_ms = s_rtc.uptime_ms;
        s_info.epoch_sec = s_rtc.epoch_sec;
        s_info.heap_free = s_rtc.heap_free;
        // backtrace is read directly from s_rtc via pal_crash_log_get_backtrace()
    }
    // If magic is 0 (crash before first tick), s_info fields stay 0.
}

extern "C" void pal_crash_log_tick(uint32_t uptime_ms, uint32_t epoch_sec)
{
    s_rtc.uptime_ms = uptime_ms;
    s_rtc.epoch_sec = epoch_sec;
    // Update heap here; the panic handler doesn't overwrite these fields
    s_rtc.heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    // Ensure magic is set so a crash after the first tick is detected
    s_rtc.magic = CRASH_MAGIC;
}

extern "C" bool pal_crash_log_pending(void)
{
    return s_pending;
}

extern "C" void pal_crash_log_get(pal_crash_info_t *out)
{
    if (out) *out = s_info;
}

extern "C" const char *pal_crash_log_get_backtrace(void)
{
    return (s_pending && s_rtc.magic == CRASH_MAGIC) ? s_rtc.backtrace : "";
}

extern "C" void pal_crash_log_clear(void)
{
    s_pending        = false;
    s_rtc.magic      = 0;
    s_rtc.backtrace[0] = '\0';
    s_info           = {};
}
