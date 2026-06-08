// module_printing.h
//
// ModulePrinting — receipt and totalizer print client.
//
// Overview
// ────────
// Subscribes to MsgFuelPumped, MsgTotalizerData, and MsgPrinterBtn
// (NOTIFICATION), then drives the ModuleHttp session-based HTTP client
// to POST JSON bodies to the configured printer endpoint.
//
// Short press (BTN_SHORT_PRESS):
//   Posts a fuel receipt to <printer_url> (JSON with vol, price, NE_ID, etc.)
//   Repeats up to printer_copy_count times per pump transaction.
//
// Long press (BTN_LONG_PRESS):
//   Posts a totalizer report to <printer_url>-totalizer (JSON with volume string).
//
// Pending-array design
// ────────────────────
// Each nozzle has one pending slot.  If a button press arrives while the
// module is already executing an HTTP call the event is queued.  When the
// call completes _try_schedule() is called to see if more events are due.
//
// Print-delay
// ───────────
// cfg.print_delay_ms introduces a programmable hold-off between the
// button press and the actual HTTP call.  If it is 0 the call fires
// immediately.  ModuleTimer is used to implement the delay.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "btn_press_type.h"
#include "fuel_config.h"
#include "msg_totalizer_data.h"
#include "msg_printer_btn.h"
#include "msg_timer_alarm.h"
#include "msg_http_result.h"
#include "msg_fuel_print_status.h"
#include "app_config.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_PRINTING_NAME  "mod_prnt"   ///< exactly 8 chars

// ---------------------------------------------------------------------------
// ModulePrinting
// ---------------------------------------------------------------------------

class ModulePrinting : public HsysModule
{
public:
    ModulePrinting() : HsysModule(MODULE_PRINTING_ID, MODULE_PRINTING_NAME) {}

    static ModulePrinting *instance();

protected:
    void pre_init() override;
    void init()     override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── State machine ────────────────────────────────────────────────────────
    enum State { WAIT_CONFIG, IDLE, HTTP };
    State _state = WAIT_CONFIG;

    // ── Per-nozzle cached pump event data ────────────────────────────────────
    struct NozzleRecord {
        bool     valid          = false;
        uint32_t vol_lx1000     = 0;
        uint32_t unit_pricex100 = 0;
        uint32_t total_pricex100= 0;
        uint64_t ne_id          = 0;
        uint32_t time_stamp     = 0;
        uint32_t print_count    = 0;   ///< Successful prints for this pump event
    };

    // ── Pending print slot (one per nozzle) ──────────────────────────────────
    struct PendingPrint {
        bool        valid           = false;
        btn_press_t btn_type        = BTN_SHORT_PRESS;
        uint64_t    recv_ms         = 0;   ///< pal_time_get_ms() when button arrived
        // Data snapshot captured at button-press time
        uint32_t    vol_lx1000      = 0;
        uint32_t    unit_pricex100  = 0;
        uint32_t    total_pricex100 = 0;
        uint64_t    ne_id           = 0;
        uint32_t    time_stamp      = 0;
        uint32_t    copy_number     = 1;   ///< 1=original, 2=Copy-1, …
        char        totalizer_str[16] = {};
    };

    // ── Active HTTP print (for retry tracking) ───────────────────────────────
    struct ActivePrint {
        bool        valid           = false;
        uint8_t     nozzle_idx      = 0;
        bool        is_totalizer    = false;
        uint8_t     retry_count     = 0;
        uint32_t    copy_number     = 1;
        uint32_t    vol_lx1000      = 0;
        uint32_t    unit_pricex100  = 0;
        uint32_t    total_pricex100 = 0;
        uint64_t    ne_id           = 0;
        uint32_t    time_stamp      = 0;
        char        totalizer_str[16] = {};
    };

    // ── Cached configuration snapshot ────────────────────────────────────────
    struct Config {
        char     printer_url[128]                     = {};
        uint32_t printer_copy_count                   = 1;
        uint32_t print_delay_ms                       = 0;
        bool     enable_nid_print                     = false;
        char     nozzle_ids[FUEL_MAX_NOZZLES][8]      = {};
    };

    NozzleRecord _nozzle_rec[FUEL_MAX_NOZZLES];
    PendingPrint _pending[FUEL_MAX_NOZZLES];
    bool         _totalizer_valid[FUEL_MAX_NOZZLES]   = {};
    char         _totalizer_str[FUEL_MAX_NOZZLES][16] = {};
    ActivePrint  _active;
    Config       _cfg;

    static constexpr int k_max_retries = 2;

    // ── Message handlers ─────────────────────────────────────────────────────
    void _on_config_ready();
    void _on_fuel_pumped(const hsys_msg_t &msg);
    void _on_totalizer_data(const hsys_msg_t &msg);
    void _on_printer_btn(const hsys_msg_t &msg);
    void _on_timer_alarm();
    void _on_http_result(const hsys_msg_t &msg);

    // ── Scheduling helpers ───────────────────────────────────────────────────

    /// Run one scheduling cycle: start HTTP for first expired pending or arm timer.
    void _try_schedule();

    /// If any pending event's delay has elapsed, start HTTP for it and return true.
    bool _process_one_expired();

    /// Arm a one-shot timer for the nearest not-yet-expired pending event.
    void _arm_timer_for_next();

    // ── HTTP helpers ─────────────────────────────────────────────────────────

    /// Capture pending[i] into _active and fire the first HTTP send.
    void _start_http_print(uint8_t nozzle_idx, const PendingPrint &pend);

    /// Build and send a single MsgHttpRequest using data already in _active.
    void _send_http_request();

    /// Build the JSON body into buf for the current _active print.
    void _build_body(char *buf, uint32_t buf_len) const;

    /// Publish MsgFuelPrintStatus for the finished print.
    void _publish_print_status(print_status_t status);
};
