// module_sysmon.cpp
//
// System Monitor — polls hsys_pool and message-header stats, prints report.

#include "module_sysmon.h"
#include "hsys_pool.h"
#include "hsys_msg.h"
#include "msg_tick_1000ms.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static ModuleSysmon s_instance;

ModuleSysmon *module_sysmon_instance(void) { return &s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void ModuleSysmon::init()
{
    printf("[%s] init\n", name());
    subscribe(MsgTick1000ms::ID);
}

// ---------------------------------------------------------------------------
// Runtime message handler
// ---------------------------------------------------------------------------

void ModuleSysmon::on_msg_received(const hsys_msg_t &msg)
{
    if (msg.msg_id != MsgTick1000ms::ID) return;

    ++m_tick_count;

    if (m_tick_count % SYSMON_REPORT_INTERVAL_TICKS == 0) {
        print_report();
    }
}

// ---------------------------------------------------------------------------
// Report printer
// ---------------------------------------------------------------------------

void ModuleSysmon::print_report()
{
    printf("\n[%s] ── Pool status  (t=%lu s) ─────────────────────────\n",
           name(), (unsigned long)m_tick_count);

    printf("  %-6s  %-10s  %-6s  %-6s  %-6s  %s\n",
           "class", "block_size", "total", "free", "used", "util%");
    printf("  %-6s  %-10s  %-6s  %-6s  %-6s  %s\n",
           "-----", "----------", "-----", "----", "----", "-----");

    uint8_t idx = 0;
    hsys_pool_class_info_t info;

    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        uint16_t used = info.total_count - info.free_count;
        uint32_t util = (info.total_count > 0)
                        ? (uint32_t)used * 100u / info.total_count
                        : 0u;

        printf("  %-6u  %-10u  %-6u  %-6u  %-6u  %lu%%\n",
               idx,
               (unsigned)info.block_size,
               (unsigned)info.total_count,
               (unsigned)info.free_count,
               (unsigned)used,
               (unsigned long)util);
        ++idx;
    }

    // Message-header pool stats
    hsys_msg_header_pool_info_t hdr;
    hsys_msg_get_header_pool_info(&hdr);

    printf("\n[%s] ── Msg-header pool ─────────────────────────────────\n",
           name());
    printf("  slots=%-4u  free=%-4u  used=%-4u  peak_used=%-4u\n",
           (unsigned)hdr.total_slots,
           (unsigned)hdr.free_slots,
           (unsigned)hdr.used_slots,
           (unsigned)hdr.peak_used_slots);
    printf("[%s] ─────────────────────────────────────────────────────\n\n",
           name());
}
