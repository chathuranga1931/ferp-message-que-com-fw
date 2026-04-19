// module_timer.cpp
//
// ModuleTimer implementation.
//
// Lifecycle:
//   pre_init()          — create the 100 ms soft-timer (do not start yet)
//   init()              — subscribe to TIMER_START + TIMER_STOP; start tick
//   on_msg_received()   — handle TIMER_START / TIMER_STOP
//   _on_tick()          — walk slots, fire alarms, rearm or clear

#include "module_timer.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_stop_response.h"
#include "msg_timer_alarm.h"
#include "pal_time.h"
#include "pal_logger.h"

#include <string.h>

#define __TAG__ "MOD_TIMR"
#ifndef MOD_TIMER_LOG_EN
#define MOD_TIMER_LOG_EN true
#endif

// ── Singleton ────────────────────────────────────────────────────────────────

static ModuleTimer s_instance;
ModuleTimer *ModuleTimer::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleTimer::pre_init()
{
    // Create the periodic soft-timer; start it in init() after subscribing.
    m_tick_timer = hsys_timer_create(
        "tmr_tick",
        k_timer_tick_ms,
        /*auto_reload=*/true,
        /*user_data=*/this,
        _tick_cb);

    if (m_tick_timer == nullptr) {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN, "failed to create tick timer");
    }

    memset(m_slots,            0, sizeof(m_slots));
    memset(m_start_uptime_ms,  0, sizeof(m_start_uptime_ms));
}

void ModuleTimer::init()
{
    subscribe(MSG_ID_TIMER_START);
    subscribe(MSG_ID_TIMER_STOP);
    LOG_MSG_INFO(MOD_TIMER_LOG_EN,
                 "subscribed TIMER_START + TIMER_STOP  (max slots: %d)",
                 MODULE_TIMER_MAX_SLOTS);

    if (m_tick_timer) {
        hsys_start_timer(m_tick_timer);
        LOG_MSG_INFO(MOD_TIMER_LOG_EN, "tick timer started (%u ms)", k_timer_tick_ms);
    }
}

// ── Soft-timer callback (runs in a background thread) ─────────────────────────

void ModuleTimer::_tick_cb(void *timer_handle)
{
    // hsys_soft_timer passes the timer handle (FreeRTOS convention);
    // retrieve the actual user_data (ModuleTimer*) via the PAL accessor.
    void *user_data = hsys_timer_get_userdata(timer_handle);
    static_cast<ModuleTimer *>(user_data)->_on_tick();
}

void ModuleTimer::_on_tick()
{
    uint64_t now_ms = pal_time_get_ms();

    for (int i = 0; i < MODULE_TIMER_MAX_SLOTS; ++i) {
        timer_meta_t &slot = m_slots[i];
        if (!slot.active) continue;
        if (now_ms < slot.next_fire_ms) continue;

        // Compute elapsed since the slot was started
        uint32_t elapsed = (uint32_t)(now_ms - m_start_uptime_ms[i]);

        _send_alarm(i, elapsed);

        if (slot.is_repetitive) {
            // Rearm: advance next_fire relative to when it *should* have fired
            // to avoid drift.
            slot.next_fire_ms += slot.duration_ms;
        } else {
            // One-shot — release the slot
            memset(&slot, 0, sizeof(slot));
            m_start_uptime_ms[i] = 0;
            LOG_MSG_INFO(MOD_TIMER_LOG_EN, "slot %d: one-shot fired, released", i);
        }
    }
}

// ── Message handler ───────────────────────────────────────────────────────────

void ModuleTimer::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_TIMER_START: {
            auto p      = MsgTimerStart::deserialize(msg);
            auto result = _start_timer(p);
            _send_start_response(p.source_module_id, result);
            break;
        }

        case MSG_ID_TIMER_STOP: {
            auto p      = MsgTimerStop::deserialize(msg);
            auto result = _stop_timer(p.source_module_id);
            _send_stop_response(p.source_module_id, result);
            break;
        }

        default:
            break;
    }
}

// ── Slot management ───────────────────────────────────────────────────────────

int ModuleTimer::_find_slot(hsys_module_id_t source_id) const
{
    for (int i = 0; i < MODULE_TIMER_MAX_SLOTS; ++i) {
        if (m_slots[i].active && m_slots[i].source_id == source_id) return i;
    }
    return -1;
}

int ModuleTimer::_free_slot() const
{
    for (int i = 0; i < MODULE_TIMER_MAX_SLOTS; ++i) {
        if (!m_slots[i].active) return i;
    }
    return -1;
}

timer_result_t ModuleTimer::_start_timer(const MsgTimerStart::Payload &p)
{
    if (p.source_module_id == HSYS_MODULE_ID_INVALID || p.duration_ms == 0) {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN, "start: invalid params (id=%u dur=%u)",
                      (unsigned)p.source_module_id, (unsigned)p.duration_ms);
        return TIMER_RESULT_ERR_INVALID_PARAM;
    }

    if (_find_slot(p.source_module_id) >= 0) {
        LOG_MSG_WARNING(MOD_TIMER_LOG_EN,
                     "start: slot already running for module %u",
                     (unsigned)p.source_module_id);
        return TIMER_RESULT_ERR_ALREADY_RUNNING;
    }

    int idx = _free_slot();
    if (idx < 0) {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN, "start: all %d slots full", MODULE_TIMER_MAX_SLOTS);
        return TIMER_RESULT_ERR_SLOTS_FULL;
    }

    uint64_t now_ms = pal_time_get_ms();
    timer_meta_t &slot     = m_slots[idx];
    slot.source_id         = p.source_module_id;
    slot.duration_ms       = p.duration_ms;
    slot.is_repetitive     = p.is_repetitive;
    slot.active            = true;
    slot.next_fire_ms      = now_ms + p.start_offset_ms + p.duration_ms;
    m_start_uptime_ms[idx] = now_ms;

    LOG_MSG_INFO(MOD_TIMER_LOG_EN,
                 "start: slot %d for module %u  dur=%u ms  offset=%u ms  rep=%d",
                 idx, (unsigned)p.source_module_id,
                 (unsigned)p.duration_ms, (unsigned)p.start_offset_ms,
                 (int)p.is_repetitive);

    return TIMER_RESULT_OK;
}

timer_result_t ModuleTimer::_stop_timer(hsys_module_id_t source_id)
{
    int idx = _find_slot(source_id);
    if (idx < 0) {
        LOG_MSG_WARNING(MOD_TIMER_LOG_EN,
                     "stop: no active slot for module %u", (unsigned)source_id);
        return TIMER_RESULT_ERR_NOT_FOUND;
    }

    memset(&m_slots[idx], 0, sizeof(m_slots[idx]));
    m_start_uptime_ms[idx] = 0;
    LOG_MSG_INFO(MOD_TIMER_LOG_EN, "stop: slot %d released for module %u",
                 idx, (unsigned)source_id);

    return TIMER_RESULT_OK;
}

// ── Response + alarm helpers ──────────────────────────────────────────────────

void ModuleTimer::_send_start_response(hsys_module_id_t dest, timer_result_t result)
{
    MsgTimerStartResponse::Payload p{};
    p.source_module_id = dest;
    p.result           = result;

    hsys_msg_t *msg = MsgTimerStartResponse::create(id(), p);
    if (msg) {
        send(msg, dest);
    } else {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN, "failed to create TimerStartResponse");
    }
}

void ModuleTimer::_send_stop_response(hsys_module_id_t dest, timer_result_t result)
{
    MsgTimerStopResponse::Payload p{};
    p.source_module_id = dest;
    p.result           = result;

    hsys_msg_t *msg = MsgTimerStopResponse::create(id(), p);
    if (msg) {
        send(msg, dest);
    } else {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN, "failed to create TimerStopResponse");
    }
}

void ModuleTimer::_send_alarm(int slot_index, uint32_t elapsed_ms)
{
    const timer_meta_t &slot = m_slots[slot_index];

    MsgTimerAlarm::Payload p{};
    p.source_module_id = slot.source_id;
    p.elapsed_ms       = elapsed_ms;

    hsys_msg_t *msg = MsgTimerAlarm::create(id(), p);
    if (msg) {
        send(msg, slot.source_id);
        LOG_MSG_INFO(MOD_TIMER_LOG_EN,
                     "alarm → module %u  elapsed=%u ms",
                     (unsigned)slot.source_id, (unsigned)elapsed_ms);
    } else {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN,
                      "failed to create TimerAlarm for module %u",
                      (unsigned)slot.source_id);
    }
}
