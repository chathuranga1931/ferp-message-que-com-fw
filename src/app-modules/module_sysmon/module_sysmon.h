// module_sysmon.h
//
// System Monitor module — polls pool and message-header stats periodically
// and prints a formatted report to stdout.
//
// This module does not subscribe to any application messages.
// It uses the MsgTick1000ms heartbeat to drive its reporting interval so it
// stays synchronised with the rest of the system without needing its own timer.
//
// Sample output (every SYSMON_REPORT_INTERVAL_TICKS seconds):
//
//  ┌─────────────────────────────────────────────────────┐
//  │ [sysmon] ── Pool status ─────────────────────────── │
//  │  class  block_size  total  free  used  utilisation  │
//  │    0         4        8     8     0      0 %         │
//  │    1        32       16    14     2     12 %         │
//  │  ...                                                 │
//  │ [sysmon] ── Msg-header pool ─────────────────────── │
//  │  slots=32  free=30  used=2                          │
//  └─────────────────────────────────────────────────────┘
//
// Task binding: "sysmon_task"

#ifndef MODULE_SYSMON_H
#define MODULE_SYSMON_H

#include "hsys_module.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define SYSMON_MODULE_NAME  "sysmon"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/** Print a report every N ticks of MsgTick1000ms (default = 5 s). */
#ifndef SYSMON_REPORT_INTERVAL_TICKS
#define SYSMON_REPORT_INTERVAL_TICKS   5
#endif

// ---------------------------------------------------------------------------
// ModuleSysmon class
// ---------------------------------------------------------------------------

class ModuleSysmon : public HsysModule
{
public:
    ModuleSysmon() : HsysModule(MODULE_SYSMON_ID, SYSMON_MODULE_NAME) {}

protected:
    // Phase 2 — subscribe to the 1 s tick
    void init() override;

    // Runtime — count ticks; print report every SYSMON_REPORT_INTERVAL_TICKS
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    void print_report();

    uint32_t m_tick_count = 0;
};

// ---------------------------------------------------------------------------
// Singleton accessor
// ---------------------------------------------------------------------------

ModuleSysmon *module_sysmon_instance(void);

#endif // MODULE_SYSMON_H
