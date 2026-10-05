// ModulePrinter.h
//
// ModulePrinter — ESC/POS receipt printer (shared by every printer product).
//
// Runs in its own task (printing blocks: ~1 s per receipt at 9600 baud).
//
// Inbound (all DIRECT):
//   MsgPrintJob          → render receipt/totalizer/sample/info → transport
//                          → MsgPrintResult DIRECT to the sender
//   MsgPrinterCmd        → sample / reset bill counter / info slip / beep
//                          → MsgPrinterStatus DIRECT to the sender
//   MsgPrinterGetStatus  → MsgPrinterStatus DIRECT to the sender
// Notifications:
//   MsgConfigReady       → (re)start the transport with the configured baud,
//                          load the bill counter (first time only)
//   MsgWifiEvent(GOT_IP) → print the boot/info slip once per boot (en_boot_print)
//
// Bill counter: persisted in SPIFFS "Logs/stats.json" ({"print_count":"N"}) —
// the same file and format as the legacy Arduino firmware, so the count
// continues across the upgrade.  It is incremented before each receipt.
//
// Receipt settings are read straight from the live app config (prn_* fields
// of app_config_t) at print time, so config changes apply to the next job.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "printer_transport.h"
#include "receipt_renderer.h"
#include "printer_types.h"
#include <stdint.h>

#define MODULE_PRINTER_NAME   "printer"

class ModulePrinter : public HsysModule
{
public:
    static constexpr size_t   RENDER_BUF_SIZE  = 4096;
    static constexpr uint32_t WRITE_TIMEOUT_MS = 15000;

    ModulePrinter() : HsysModule(MODULE_PRINTER_ID, MODULE_PRINTER_NAME) {}
    static ModulePrinter *instance();

    /** Select the printer link.  Call from app_init() before the framework starts. */
    void set_transport(IPrinterTransport *t) { _transport = t; }

    /** Buzzer GPIO (active-high), -1 = no buzzer.  Call before the framework starts. */
    void set_buzzer_gpio(int32_t gpio) { _buzzer_gpio = gpio; }

    /** Thread-safe snapshot for the legacy HTTP API. */
    uint32_t print_count() const { return _print_count; }

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    IPrinterTransport *_transport   = nullptr;
    int32_t            _buzzer_gpio = -1;

    uint32_t          _print_count  = 0;   ///< word-sized: safe to read from other tasks
    bool              _count_loaded = false;
    bool              _info_printed = false;
    uint32_t          _jobs_ok      = 0;
    uint32_t          _jobs_failed  = 0;
    uint32_t          _last_job_id  = 0;
    uint8_t           _last_result  = PRINT_RESULT_OK;
    char              _ip[16]       = {};

    uint8_t           _buf[RENDER_BUF_SIZE];

    void _on_config_ready();
    void _on_print_job(const hsys_msg_t &msg);
    void _on_cmd(const hsys_msg_t &msg);
    void _send_status(hsys_module_id_t to, uint8_t last_cmd);

    print_result_t _print(const receipt_job_t &job, uint8_t kind);
    void           _fill_cfg(receipt_cfg_t &cfg) const;
    void           _load_count();
    void           _save_count();
    void           _beep(uint32_t seconds);
};
