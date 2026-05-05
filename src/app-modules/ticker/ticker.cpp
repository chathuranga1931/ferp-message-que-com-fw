// ticker.cpp  — Ticker implementation

#include "ticker.h"
#include "hsys_soft_timer.h"
#include "msg_tick_1000ms.h"

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static Ticker s_instance;

Ticker *Ticker::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Timer callback — runs in FreeRTOS timer daemon context
// ---------------------------------------------------------------------------

static void timer_cb(void *user_data)
{
    static_cast<Ticker *>(user_data)->tick();
}

void Ticker::tick()
{
    hsys_msg_t *msg = create_typed<MsgTick1000ms>(MsgTick1000ms::Payload{});
    if (msg == nullptr) {
        log_error("create_typed<MsgTick1000ms> failed");
        return;
    }
    publish(msg);
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void Ticker::init()
{
    log("init");

    hsys_timer_handle_t tmr = hsys_timer_create(
        "ticker_1000ms",
        TICKER_PERIOD_MS,
        true,
        this,
        timer_cb
    );

    if (tmr == nullptr || !hsys_start_timer(tmr)) {
        log_error("failed to start soft timer");
    }
}

// ---------------------------------------------------------------------------
// Runtime handler — Ticker is publish-only
// ---------------------------------------------------------------------------

void Ticker::on_msg_received(const hsys_msg_t &msg)
{
    (void)msg;
    log_error("unexpected message received");
}
