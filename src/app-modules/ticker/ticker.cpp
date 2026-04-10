// ticker.cpp  — Ticker implementation

#include "ticker.h"
#include "hsys_soft_timer.h"
#include "app_msg_table.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static Ticker s_instance;

Ticker *ticker_instance(void) { return &s_instance; }

// ---------------------------------------------------------------------------
// Timer callback — runs in FreeRTOS timer daemon context
// ---------------------------------------------------------------------------

static void timer_cb(void *user_data)
{
    static_cast<Ticker *>(user_data)->tick();
}

void Ticker::tick()
{
    hsys_msg_t *msg = create_msg(MSG_TICK_1000MS);
    if (msg == nullptr) {
        printf("[%s] ERROR: create_msg failed\n", name());
        return;
    }
    publish(msg);
}

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void Ticker::init()
{
    printf("[%s] init\n", name());

    hsys_timer_handle_t tmr = hsys_timer_create(
        "ticker_1000ms",
        TICKER_PERIOD_MS,
        true,       // auto-reload
        this,       // user_data → passed back to timer_cb
        timer_cb
    );

    if (tmr == nullptr || !hsys_start_timer(tmr)) {
        printf("[%s] ERROR: failed to start soft timer\n", name());
    }
}

// ---------------------------------------------------------------------------
// Runtime handler — Ticker is publish-only
// ---------------------------------------------------------------------------

void Ticker::on_msg_received(const hsys_msg_t &msg)
{
    (void)msg;
    printf("[%s] unexpected message received\n", name());
}
