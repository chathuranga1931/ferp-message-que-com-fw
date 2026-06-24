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
// IRAM placement
// ──────────────
// The wrapper and every function it calls are marked IRAM_ATTR.  This is
// mandatory for correctness: when the interrupt watchdog (int_wdt) fires at
// level-5 priority, the panic handler disables the flash cache before
// invoking esp_panic_handler().  Any code reached through that call that
// lives in flash would cause a cache fault, triggering a double-exception →
// RWDT hardware reset → RTC SRAM cleared → crash data lost.
//
// Placing the wrapper in IRAM means it executes with the cache in any state.
// The hex formatter uses inline arithmetic (no lookup table in flash).
// The stack walker is reimplemented locally (avoids esp_backtrace_get_next_frame
// which is not IRAM_ATTR in IDF 6.0.1).
//
// After the reboot, pal_crash_log_init() detects the crash via
// esp_reset_reason(), and the RTC data is available for writing to SD and
// sending to the cloud.

#include "pal_crash_log.h"

// IDF panic internals — available because esp_system is in PRIV_REQUIRES
#include "esp_private/panic_internal.h"
// IDF system headers
#include <string.h>             // strncpy
#include <esp_system.h>
#include <esp_timer.h>          // IRAM_ATTR in IDF — safe to call from wrapper
#include <esp_heap_caps.h>
#include "hal/wdt_hal.h"         // wdt_hal_context_t — downgrade RWDT RESET_RTC stage

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

// RTC_NOINIT_ATTR (.rtc_noinit section) is critical here.
// cpu_start.c clears _rtc_bss_start.._rtc_bss_end on EVERY non-deep-sleep reset
// including SW_CPU_RESET (task_wdt panic).  RTC_DATA_ATTR places variables in
// .rtc.data/.rtc.bss which falls inside that range and is wiped before our code
// ever runs.  RTC_NOINIT_ATTR places the variable in .rtc_noinit which is
// outside that range and is preserved across all resets except a full power-on.
// The magic field check in pal_crash_log_init() handles the garbage-data case
// on first power-on.
static RTC_NOINIT_ATTR crash_rtc_t s_rtc;

static bool             s_pending          = false;
static pal_crash_info_t s_info             = {};
// Backtrace snapshot taken at pal_crash_log_init() time.
// Stored separately so that pal_crash_log_tick() overwriting s_rtc fields
// cannot corrupt or falsely validate the backtrace that was present at boot.
static char             s_info_backtrace[CRASH_BACKTRACE_SIZE] = {};

// ── Xtensa exception frame (ABI-stable for ESP32/S2/S3) ──────────────────────

typedef struct {
    uint32_t exit;
    uint32_t pc;
    uint32_t ps;
    uint32_t a0;   uint32_t a1;
    uint32_t a2;   uint32_t a3;
    uint32_t a4;   uint32_t a5;
    uint32_t a6;   uint32_t a7;
    uint32_t a8;   uint32_t a9;
    uint32_t a10;  uint32_t a11;
    uint32_t a12;  uint32_t a13;
    uint32_t a14;  uint32_t a15;
    uint32_t sar;
    uint32_t exccause;
    uint32_t excvaddr;
} crash_frame_t;

// ── panic_info_t compatible struct (verified against IDF 6.0.1 layout) ───────

typedef void (*crash_dump_fn_t)(const void *frame);

typedef struct {
    int             core;
    int             exception;
    const char     *reason;
    const char     *description;
    crash_dump_fn_t details;
    crash_dump_fn_t state;
    const void     *addr;
    const void     *frame;
    bool            pseudo_excause;
} crash_info_t;

// ── Forward declaration ───────────────────────────────────────────────────────

extern "C" void __attribute__((noreturn)) __real_esp_panic_handler(crash_info_t *info);

// ── PC processing for Xtensa windowed ABI ────────────────────────────────────
// inline → gets placed in the IRAM section of whichever IRAM_ATTR caller uses it.

static inline uint32_t _process_pc(uint32_t pc)
{
    if (pc & 0x80000000) {
        pc = (pc & 0x3fffffff) | 0x40000000;
    }
    return pc - 3;
}

// ── IRAM-safe hex formatter ───────────────────────────────────────────────────
// Uses inline arithmetic — no lookup table in flash that would require cache.

static IRAM_ATTR void _append_hex32(char *buf, size_t buf_size, size_t *pos, uint32_t val)
{
    if (*pos + 10 >= buf_size) return;  // "0x" + 8 hex + guard
    buf[(*pos)++] = '0';
    buf[(*pos)++] = 'x';
    for (int i = 7; i >= 0; i--) {
        uint32_t nibble = (val >> (i * 4)) & 0xFU;
        buf[(*pos)++] = (char)(nibble < 10u ? '0' + nibble : 'a' + nibble - 10u);
    }
}

// ── IRAM-safe Xtensa stack frame walker ──────────────────────────────────────
// Reimplements esp_backtrace_get_next_frame() locally so we never call into
// flash from IRAM context.  Xtensa windowed ABI stores the caller's a0
// (return address) at sp-16 and caller's sp at sp-12.
//
// SP bounds: covers all internal SRAM (DRAM + IRAM region) plus RTC fast
// memory on ESP32.  ISR stacks live at the high end of DRAM; task stacks sit
// lower.  An sp outside this window means the walk has gone off the rails.

#define _BT_SP_MIN 0x3FF80000UL
#define _BT_SP_MAX 0x40000000UL
#define _BT_SP_OK(sp) ((sp) >= _BT_SP_MIN && (sp) < _BT_SP_MAX && ((sp) & 3) == 0)

typedef struct {
    uint32_t pc;
    uint32_t sp;
    uint32_t next_pc;   ///< a0 of current frame = return addr of its caller
} _bt_frame_t;

static IRAM_ATTR bool _bt_next_frame(_bt_frame_t *f)
{
    if (!_BT_SP_OK(f->sp)) return false;
    // sp - 16 must also be within the valid window; otherwise the read would
    // land below _BT_SP_MIN and may fault, causing a double-exception that
    // triggers RWDT RESET_RTC, wiping RTC SRAM before we can save crash data.
    if (f->sp < _BT_SP_MIN + 16) return false;
    uint32_t prev_a0 = *((volatile uint32_t *)(f->sp - 16));
    uint32_t prev_sp = *((volatile uint32_t *)(f->sp - 12));
    f->pc      = f->next_pc;
    f->next_pc = prev_a0;
    f->sp      = prev_sp;
    return (f->pc != 0 && f->sp != 0);
}

// ── IRAM backtrace string builder ────────────────────────────────────────────

static IRAM_ATTR void _build_backtrace(const crash_frame_t *frame,
                                       char *buf, size_t buf_size)
{
    if (!frame || buf_size < 2) { buf[0] = '\0'; return; }

    // Reject obviously invalid initial stack pointer before dereferencing it.
    if (!_BT_SP_OK(frame->a1)) { buf[0] = '\0'; return; }

    _bt_frame_t bt = {
        .pc      = frame->pc,
        .sp      = frame->a1,
        .next_pc = frame->a0,
    };

    size_t pos = 0;
    for (int depth = 0; depth < 20; depth++) {
        uint32_t pc = _process_pc(bt.pc);
        _append_hex32(buf, buf_size, &pos, pc);
        if (pos < buf_size) buf[pos++] = ':';
        _append_hex32(buf, buf_size, &pos, bt.sp);
        if (pos < buf_size) buf[pos++] = ' ';
        if (!_bt_next_frame(&bt)) break;
    }
    if (pos > 0 && buf[pos - 1] == ' ') pos--;
    buf[pos < buf_size ? pos : buf_size - 1] = '\0';
}

// ── Panic handler wrapper (IRAM) ─────────────────────────────────────────────
//
// IRAM_ATTR is mandatory here.  When int_wdt fires at level-5 priority the
// cache is disabled before esp_panic_handler() is called.  If this function
// were in flash it would fault immediately, causing a double-exception that
// clears RTC SRAM via RWDT hardware reset and loses all crash data.

extern "C" IRAM_ATTR void __attribute__((noreturn))
__wrap_esp_panic_handler(crash_info_t *info)
{
    // Write uptime and magic FIRST — before the backtrace walk.
    // panic_handler.c calls esp_panic_handler_enable_rtc_wdt() which arms the
    // RWDT with WDT_STAGE_ACTION_RESET_RTC.  If anything after this point
    // causes a double-exception (or the restart cycle takes too long), the RWDT
    // fires and wipes all RTC SRAM.  Setting the magic here guarantees that at
    // least uptime_ms is valid even if the backtrace walk is skipped or aborted.
    s_rtc.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_rtc.magic     = CRASH_MAGIC;   // ← must be before _build_backtrace

    const crash_frame_t *frame = (const crash_frame_t *)info->frame;
    _build_backtrace(frame, s_rtc.backtrace, sizeof(s_rtc.backtrace));
    // epoch_sec and heap_free retain tick values — NTP/heap unsafe in panic

    // Disable the RWDT now that all crash data is written.
    // panic_handler.c called esp_panic_handler_enable_rtc_wdt() which armed
    // RWDT Stage0=RESET_RTC@10 s before invoking esp_panic_handler().
    // Disabling it here prevents that 10 s stage from firing during the
    // (potentially slow) panic print.
    // NOTE: esp_restart_noos() will re-arm RWDT with Stage0=RESET_SYSTEM@1s
    // and Stage1=RESET_RTC@1s.  The Stage1 threat is handled by
    // pal_crash_log_init() disabling the RWDT immediately on next boot.
    {
        wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
        wdt_hal_write_protect_disable(&rwdt_ctx);
        wdt_hal_disable(&rwdt_ctx);
        wdt_hal_write_protect_enable(&rwdt_ctx);
    }

    __real_esp_panic_handler(info);
}

// ── PAL API implementation ────────────────────────────────────────────────────

// Internal helper — safe to call multiple times (idempotent).
static void _disable_rwdt(void)
{
    wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rwdt_ctx);
    wdt_hal_disable(&rwdt_ctx);
    wdt_hal_write_protect_enable(&rwdt_ctx);
}

extern "C" void pal_crash_log_disable_boot_wdt(void)
{
    // Must be called as the very first instruction in app_main(), before any
    // slow peripheral init (NVS, SD, I2C, ...).
    //
    // esp_restart_noos() arms the RWDT with:
    //   Stage0 = RESET_SYSTEM @ 1 s
    //   Stage1 = RESET_RTC    @ 1 s (after Stage0 fires → 2 s total)
    // enabled via wdt_hal_set_flashboot_en(true) before triggering the CPU
    // reset.  IDF's cpu_start.c only disables the RWDT when the reset reason
    // is RWDT_RESET or MWDT_RESET.  For a software panic (task_wdt →
    // SW_CPU_RESET) the RWDT keeps counting through the entire boot sequence.
    // If Stage0 fires → RESET_SYSTEM (second boot), then Stage1 fires during
    // the second boot → RESET_RTC → all RTC SRAM is wiped and the crash log
    // is lost.
    //
    // The bootloader's wdt_hal_set_flashboot_en(false) only clears the
    // flashboot-enable bit; if the RWDT was independently enabled (WDT_EN bit
    // set by wdt_hal_init), it keeps running even after CONFIG_BOOTLOADER_WDT_ENABLE=n.
    _disable_rwdt();
}

extern "C" void pal_crash_log_init(void)
{
    // Belt-and-suspenders: disable RWDT here too in case
    // pal_crash_log_disable_boot_wdt() was not called first.
    _disable_rwdt();

    esp_reset_reason_t reason = esp_reset_reason();
    bool is_crash = (reason == ESP_RST_PANIC   ||
                     reason == ESP_RST_TASK_WDT ||
                     reason == ESP_RST_INT_WDT  ||
                     reason == ESP_RST_WDT);
    if (!is_crash) return;

    s_pending = true;
    if (s_rtc.magic == CRASH_MAGIC) {
        // RTC SRAM intact: capture the exact-crash-moment values before
        // pal_crash_log_tick() overwrites them with the new session's data.
        s_info.uptime_ms      = s_rtc.uptime_ms;
        s_info.epoch_sec      = s_rtc.epoch_sec;
        s_info.heap_free      = s_rtc.heap_free;
        s_info.rtc_was_valid  = true;
        // Snapshot the backtrace now — after the first tick s_rtc.magic will
        // be CRASH_MAGIC again (set by tick) but s_rtc.backtrace will be the
        // crash-time string only if it was written by the wrap handler.  Reading
        // it here, before any tick, is the only reliable window.
        strncpy(s_info_backtrace, s_rtc.backtrace, sizeof(s_info_backtrace) - 1);
        s_info_backtrace[sizeof(s_info_backtrace) - 1] = '\0';
    } else {
        // RTC SRAM was cleared by a hardware RESET_RTC (fired by RWDT safety
        // stage in panic_handler.c or esp_restart_noos).  Numeric fields stay
        // zero; backtrace is unavailable.
        s_info.rtc_was_valid = false;
        s_info_backtrace[0]  = '\0';
    }
}

extern "C" void pal_crash_log_tick(uint32_t uptime_ms, uint32_t epoch_sec)
{
    s_rtc.uptime_ms = uptime_ms;
    s_rtc.epoch_sec = epoch_sec;
    s_rtc.heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    s_rtc.magic     = CRASH_MAGIC;
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
    // Return the snapshot taken at pal_crash_log_init(), not the live s_rtc
    // field.  After the first tick, s_rtc.magic is set again by
    // pal_crash_log_tick(), so checking s_rtc.magic here would always pass
    // even when RTC was cleared and backtrace is genuinely absent.
    return s_pending ? s_info_backtrace : "";
}

extern "C" void pal_crash_log_clear(void)
{
    s_pending               = false;
    s_rtc.magic             = 0;
    s_rtc.backtrace[0]      = '\0';
    s_info                  = {};
    s_info_backtrace[0]     = '\0';
}
