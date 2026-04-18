// module_timer.h
//
// ModuleTimer — software timer service for the HSYS application.
//
// Overview
// ────────
// ModuleTimer manages a fixed-size pool of timer slots.  Any module can
// request a timer by publishing MSG_ID_TIMER_START.  ModuleTimer allocates
// a slot, ticks it via a periodic soft-timer callback (every 100 ms), and
// sends a DIRECT MSG_ID_TIMER_ALARM to the requesting module when the slot
// fires.
//
// One slot per source_module_id.  A module that requests a second timer
// before stopping the first receives TIMER_RESULT_ERR_ALREADY_RUNNING.
//
// Slot count
// ──────────
// Default is 20.  Override at build time:
//   target_compile_definitions(... PRIVATE MODULE_TIMER_MAX_SLOTS=10)
//
// Message flow
// ────────────
//   Any module  ──MSG_ID_TIMER_START (NOTIF)──▶  ModuleTimer
//   ModuleTimer ──MSG_ID_TIMER_START_RESPONSE (DIRECT)──▶  requester
//
//   (on alarm)
//   ModuleTimer ──MSG_ID_TIMER_ALARM (DIRECT)──▶  registered module
//
//   Any module  ──MSG_ID_TIMER_STOP (NOTIF)──▶  ModuleTimer
//   ModuleTimer ──MSG_ID_TIMER_STOP_RESPONSE (DIRECT)──▶  requester

#pragma once

#include "hsys_module.h"
#include "hsys_soft_timer.h"
#include "timer_types.h"       // timer_meta_t, timer_result_t
#include "msg_timer_start.h"   // MsgTimerStart::Payload used in private helpers

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_TIMER_ID    ((hsys_module_id_t)7)
#define MODULE_TIMER_NAME  "mod_tmr"   // exactly 7 chars — within 8-char PAL limit

// ---------------------------------------------------------------------------
// Slot pool size (override via compile definition)
// ---------------------------------------------------------------------------

#ifndef MODULE_TIMER_MAX_SLOTS
#define MODULE_TIMER_MAX_SLOTS  20
#endif

// Tick period for the internal soft-timer (100 ms gives ±100 ms resolution)
static constexpr uint32_t k_timer_tick_ms = 100U;

// ---------------------------------------------------------------------------
// ModuleTimer
// ---------------------------------------------------------------------------

class ModuleTimer : public HsysModule
{
public:
    ModuleTimer() : HsysModule(MODULE_TIMER_ID, MODULE_TIMER_NAME) {}

    static ModuleTimer *instance();

protected:
    void pre_init()  override;
    void init()      override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── Soft-timer callback ──────────────────────────────────────────────────
    static void _tick_cb(void *user_data);
    void        _on_tick();

    // ── Slot management ──────────────────────────────────────────────────────
    int          _find_slot(hsys_module_id_t source_id) const;
    int          _free_slot() const;
    timer_result_t _start_timer(const struct MsgTimerStart::Payload &p);
    timer_result_t _stop_timer(hsys_module_id_t source_id);

    // ── Helpers ──────────────────────────────────────────────────────────────
    void _send_start_response(hsys_module_id_t dest, timer_result_t result);
    void _send_stop_response(hsys_module_id_t dest, timer_result_t result);
    void _send_alarm(int slot_index, uint32_t elapsed_ms);

    // ── State ────────────────────────────────────────────────────────────────
    timer_meta_t         m_slots[MODULE_TIMER_MAX_SLOTS] = {};
    hsys_timer_handle_t  m_tick_timer = nullptr;
    uint64_t             m_start_uptime_ms[MODULE_TIMER_MAX_SLOTS] = {};
};
