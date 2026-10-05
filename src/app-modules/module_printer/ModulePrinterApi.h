// ModulePrinterApi.h
//
// ModulePrinterApi — the legacy printer HTTP API (FERP printer firmware 42.x),
// served by ModuleWebServer on the printer's web port via set_extra_routes().
//
//   POST /print                 → MsgPrintJob (receipt)    → ModulePrinter
//   POST /print-totalizer       → MsgPrintJob (totalizer)  → ModulePrinter
//   GET  /printSample           → MsgPrintJob (sample)     → ModulePrinter
//   GET  /erasePrintCount       → MsgPrinterCmd(RESET_COUNT)
//   GET  /getDeviceInformation  → {"ssid","ipaddress","rssi","device_version","displayvalue"}
//   GET  /getMainThreadCounter  → {"main_thread_counter":"<uptime s>"}
//   GET  /reboot                → "Device reboot", then MsgSystemReboot
//
// Also provided elsewhere:
//   GET  /getDeviceConfigurations, POST /setDeviceConfigurationsPost — ModuleWebServer
//   POST /updatePrinterFirmwareBin — ModuleWebServer firmware-upload alias
//
// The /print bodies are what the FERP COM unit (ModulePrinting) sends:
//   {"time","print_time","print_note","nozzel_id","measurements":{"L","T","P","U","NE_ID"}}
// The handlers answer 200 as soon as the job is queued (as the legacy firmware did).

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "ModuleWebServer.h"

#define MODULE_PRINTER_API_NAME  "prn_api"

class ModulePrinterApi : public HsysModule
{
public:
    ModulePrinterApi() : HsysModule(MODULE_PRINTER_API_ID, MODULE_PRINTER_API_NAME) {}
    static ModulePrinterApi *instance();

    /** Route table for ModuleWebServer::set_extra_routes(). */
    static const ModuleWebServer::ExtraRouteDef *routes();

protected:
    void on_msg_received(const hsys_msg_t &) override {}

private:
    uint32_t _job_seq = 0;

    static int32_t _hdl_print          (pal_http_request_t req, void *ctx);
    static int32_t _hdl_print_totalizer(pal_http_request_t req, void *ctx);
    static int32_t _hdl_print_sample   (pal_http_request_t req, void *ctx);
    static int32_t _hdl_erase_count    (pal_http_request_t req, void *ctx);
    static int32_t _hdl_device_info    (pal_http_request_t req, void *ctx);
    static int32_t _hdl_thread_counter (pal_http_request_t req, void *ctx);
    static int32_t _hdl_reboot         (pal_http_request_t req, void *ctx);

    int32_t _queue_print(pal_http_request_t req, uint8_t kind);
};
