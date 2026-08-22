#pragma once

#include "hsys_config.h"
#include "hsys_task_mgr.h"   // hsys_task_desc_t
#include "hsys_module.h"     // HsysModule
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Core lifecycle (called from every product main) ──────────────────────────

/** Full application init — config, pool, modules, messages, tasks. */
void app_init(void);

/** Application run hook — called in the main loop.
 *  Default implementation: empty (FreeRTOS scheduler owns the CPU).
 *  Override per-platform (e.g. simulator overrides to nanosleep). */
void app_run(void);

// ── Platform extension hook ───────────────────────────────────────────────────

/** Called by app_init() before any framework init.
 *  Default implementation: empty (weak symbol).
 *  Override in the platform-specific main.cpp to:
 *    - Do platform setup (chdir, logger init, hardware init, …)
 *    - Call app_register_extra_module() for platform-only modules. */
void app_platform_pre_init(void);

/** Register a platform-specific module + its task.
 *  Must be called from app_platform_pre_init().
 *  The task_desc pointer must remain valid for the lifetime of the app. */
void app_register_extra_module(HsysModule              *module,
                                const hsys_task_desc_t  *task_desc);

// ── Config accessors (for app-layer code only) ────────────────────────────────

/** Load defaults into the live config struct.
 *  Called internally by app_init(). hsys_config_init() is owned by ModuleConfig. */
void app_config_init(void);

/** Returns a pointer to the live app_config_t (owned by app.cpp). */
const app_config_t *app_config_get(void);

/** Returns the config field table and optionally its size. */
config_t *app_config_get_table(uint16_t *out_size);

// ── Crash-recovery test helpers (not called in production) ───────────────────

/** Immediately trigger a null-pointer panic. Device reboots, detects the crash,
 *  writes crash.json to SD, and sends a core/crash event to the cloud.
 *  No-op on the simulator. */
void app_test_trigger_panic(void);

/** Spin-lock the calling task to trigger a task watchdog (~10 s).
 *  Device reboots, detects the WDT crash, and follows the same SD + cloud path.
 *  No-op on the simulator. */
void app_test_trigger_task_wdt(void);

#ifdef __cplusplus
}
#endif
