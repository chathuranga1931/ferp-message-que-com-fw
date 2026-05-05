// ticker.h
//
// Ticker — System heartbeat publisher
//
// Inherits HsysModule.  Overrides:
//   init() → creates and starts a 1000ms auto-reload soft timer
//               whose callback publishes MSG_TICK_1000MS.
//
// Task binding: "ticker_task"

#ifndef TICKER_H
#define TICKER_H

#include "hsys_module.h"
#include "app_module_ids.h"
#define TICKER_MODULE_NAME  "ticker"
#define TICKER_PERIOD_MS    1000U

class Ticker : public HsysModule
{
public:
    Ticker() : HsysModule(TICKER_MODULE_ID, TICKER_MODULE_NAME) {}

    /** Called by the soft-timer callback to publish the tick message. */
    void tick();

    static Ticker *instance();

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;
};

#endif // TICKER_H
