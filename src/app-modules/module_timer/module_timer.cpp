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

    // Mutex guards m_slots[] and m_start_uptime_ms[] against the data race
    // between _on_tick() (soft-timer thread) and on_msg_received() (timing_task).
    m_slots_mutex = hsys_mutex_create();

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

    // Snapshot all due alarms under the mutex so the lock is held only briefly
    // (never while blocking on hsys_queue_send).  This eliminates the data race
    // between this soft-timer thread and the timing_task dispatch thread that
    // calls on_msg_received() → _start_timer() / _stop_timer().
    struct pending_t {
        hsys_module_id_t dest;
        uint32_t         elapsed_ms;
        uint32_t         user_tag;
        int              slot_index;
    };
    pending_t pending[MODULE_TIMER_MAX_SLOTS];
    int       pending_count = 0;

    hsys_mutex_lock(m_slots_mutex);
    for (int i = 0; i < MODULE_TIMER_MAX_SLOTS; ++i) {
        timer_meta_t &slot = m_slots[i];
        if (!slot.active) continue;
        if (now_ms < slot.next_fire_ms) continue;

        uint32_t elapsed = (uint32_t)(now_ms - m_start_uptime_ms[i]);
        pending[pending_count++] = { slot.source_id, elapsed, slot.user_tag, i };

        if (slot.is_repetitive) {
            // Advance next_fire to avoid drift.
            slot.next_fire_ms += slot.duration_ms;
        }
    }
    hsys_mutex_unlock(m_slots_mutex);

    // Send alarms outside the lock — these may block briefly if the receiver
    // queue is momentarily full, but the mutex is free for on_msg_received().
    for (int j = 0; j < pending_count; ++j) {
        bool sent = _send_alarm_direct(pending[j].dest, pending[j].elapsed_ms, pending[j].user_tag);
        if (!sent) {
            // Keep one-shot slots armed so the alarm can be retried on the
            // next tick instead of being lost when the receiver queue is full.
            continue;
        }

        hsys_mutex_lock(m_slots_mutex);
        timer_meta_t &slot = m_slots[pending[j].slot_index];
        if (slot.active && slot.source_id == pending[j].dest && !slot.is_repetitive) {
            memset(&slot, 0, sizeof(slot));
            m_start_uptime_ms[pending[j].slot_index] = 0;
        }
        hsys_mutex_unlock(m_slots_mutex);
    }
}

// ── Message handler ───────────────────────────────────────────────────────────

void ModuleTimer::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_TIMER_START: {
            auto p = MsgTimerStart::deserialize(msg);
            hsys_mutex_lock(m_slots_mutex);
            auto result = _start_timer(p);
            hsys_mutex_unlock(m_slots_mutex);
            _send_start_response(p.source_module_id, result);
            break;
        }

        case MSG_ID_TIMER_STOP: {
            auto p = MsgTimerStop::deserialize(msg);
            hsys_mutex_lock(m_slots_mutex);
            auto result = _stop_timer(p.source_module_id);
            hsys_mutex_unlock(m_slots_mutex);
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
        if (p.forced) {
            // forced restart — clear the existing slot and fall through to allocate a new one
            _stop_timer(p.source_module_id);
            // LOG_MSG_INFO(MOD_TIMER_LOG_EN,
            //              "start: forced restart for module %u",
            //              (unsigned)p.source_module_id);
        } else {
            LOG_MSG_WARNING(MOD_TIMER_LOG_EN,
                         "start: slot already running for module %u",
                         (unsigned)p.source_module_id);
            return TIMER_RESULT_ERR_ALREADY_RUNNING;
        }
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
    slot.user_tag          = p.user_tag;
    slot.is_repetitive     = p.is_repetitive;
    // Write next_fire_ms and start time BEFORE setting active=true so that
    // _on_tick() never sees a slot that is "live" but has next_fire_ms == 0.
    slot.next_fire_ms      = now_ms + p.start_offset_ms + p.duration_ms;
    m_start_uptime_ms[idx] = now_ms;
    slot.active            = true;   // must be last: makes the slot visible to _on_tick()

    // LOG_MSG_INFO(MOD_TIMER_LOG_EN,
    //              "start: slot %d for module %u  dur=%u ms  offset=%u ms  rep=%d",
    //              idx, (unsigned)p.source_module_id,
    //              (unsigned)p.duration_ms, (unsigned)p.start_offset_ms,
    //              (int)p.is_repetitive);

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
    // LOG_MSG_INFO(MOD_TIMER_LOG_EN, "stop: slot %d released for module %u",
    //              idx, (unsigned)source_id);

    return TIMER_RESULT_OK;
}

// ── Response + alarm helpers ──────────────────────────────────────────────────

void ModuleTimer::_send_start_response(hsys_module_id_t dest, timer_result_t result)
{
    // Only send if the destination module actually subscribed to this message.
    // Unsubscribed modules (e.g. module_fuel) don't process the response and
    // the undeliverable message would just consume a queue slot needlessly.
    if (!hsys_msg_is_subscriber(MSG_ID_TIMER_START_RESPONSE, dest)) return;

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
    // Only send if the destination module actually subscribed to this message.
    if (!hsys_msg_is_subscriber(MSG_ID_TIMER_STOP_RESPONSE, dest)) return;

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

bool ModuleTimer::_send_alarm_direct(hsys_module_id_t dest, uint32_t elapsed_ms, uint32_t user_tag)
{
    MsgTimerAlarm::Payload p{};
    p.source_module_id = dest;
    p.elapsed_ms       = elapsed_ms;
    p.user_tag         = user_tag;

    hsys_msg_t *msg = MsgTimerAlarm::create(id(), p);
    if (!msg) {
        LOG_MSG_ERROR(MOD_TIMER_LOG_EN,
                      "failed to create TimerAlarm for module %u",
                      (unsigned)dest);
        return false;
    }

    // Use a 50 ms timeout so a transiently-full receiver queue doesn't cause
    // the alarm to be silently dropped.  The dispatch thread drains in <1 ms
    // under normal load, so this almost never waits.
    hsys_status_t st = send(msg, dest, 50);
    return (st == HSYS_OK);
}
