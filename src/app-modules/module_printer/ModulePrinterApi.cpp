// ModulePrinterApi.cpp — see ModulePrinterApi.h

#include "ModulePrinterApi.h"

#include "version.h"
#include "app.h"             // app_config_get()
#include "app_config.h"
#include "msg_print_job.h"
#include "msg_printer_cmd.h"
#include "msg_system_reboot.h"

#include "pal_http_server.h"
#include "pal_wifi.h"
#include "pal_time.h"
#include "pal_logger.h"
#include "hsys_task.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#define __TAG__   "PRN_API "
#define PAPI_LOG  true

static constexpr size_t k_body_max = 2048;   // legacy limit for request bodies

static ModulePrinterApi s_instance;
ModulePrinterApi *ModulePrinterApi::instance() { return &s_instance; }

const ModuleWebServer::ExtraRouteDef *ModulePrinterApi::routes()
{
    static const ModuleWebServer::ExtraRouteDef k_routes[] = {
        { "/print",                PAL_HTTP_POST, _hdl_print,           &s_instance },
        { "/print-totalizer",      PAL_HTTP_POST, _hdl_print_totalizer, &s_instance },
        { "/printSample",          PAL_HTTP_GET,  _hdl_print_sample,    &s_instance },
        { "/erasePrintCount",      PAL_HTTP_GET,  _hdl_erase_count,     &s_instance },
        { "/getDeviceInformation", PAL_HTTP_GET,  _hdl_device_info,     &s_instance },
        { "/getMainThreadCounter", PAL_HTTP_GET,  _hdl_thread_counter,  &s_instance },
        { "/reboot",               PAL_HTTP_GET,  _hdl_reboot,          &s_instance },
        { nullptr,                 PAL_HTTP_GET,  nullptr,              nullptr     },
    };
    return k_routes;
}

// ── Print endpoints ─────────────────────────────────────────────────────────

int32_t ModulePrinterApi::_queue_print(pal_http_request_t req, uint8_t kind)
{
    // Read the whole body first (legacy bug fix: the old handler acted on
    // every body chunk instead of the complete request).
    static char body[k_body_max + 1];
    size_t total = pal_http_req_get_content_len(req);
    if (total == 0 || total > k_body_max) {
        LOG_MSG_WARNING(PAPI_LOG, "print: bad body length %u", (unsigned)total);
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "bad body", 0);
    }
    size_t got = 0;
    while (got < total) {
        size_t n = 0;
        if (pal_http_req_recv(req, body + got, total - got, &n) != PAL_OK || n == 0) break;
        got += n;
    }
    body[got] = '\0';

    MsgPrintJob::Payload p{};
    if (!MsgPrintJob::payload_from_legacy_json(body, got, (print_job_kind_t)kind, &p)) {
        LOG_MSG_WARNING(PAPI_LOG, "print: body rejected (no \"time\"): %s", body);
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "bad json", 0);
    }
    p.job_id = ++_job_seq;
    p.source = PRINT_SRC_HTTP;

    hsys_msg_t *m = MsgPrintJob::create(id(), p);
    if (!m || send(m, MODULE_PRINTER_ID, 1000) != HSYS_OK) {
        LOG_MSG_ERROR(PAPI_LOG, "print: could not queue job (time %s)", p.time_stamp);
        pal_http_resp_set_status(req, 503);
        return pal_http_resp_send(req, "busy", 0);
    }
    LOG_MSG_INFO(PAPI_LOG, "queued %s job %lu (time %s, nozzle %s)",
                 kind == PRINT_JOB_TOTALIZER ? "totalizer" : "receipt",
                 (unsigned long)p.job_id, p.time_stamp, p.nozzle_id);
    return pal_http_resp_send(req, "", 0);
}

int32_t ModulePrinterApi::_hdl_print(pal_http_request_t req, void *ctx)
{
    return static_cast<ModulePrinterApi *>(ctx)->_queue_print(req, PRINT_JOB_RECEIPT);
}

int32_t ModulePrinterApi::_hdl_print_totalizer(pal_http_request_t req, void *ctx)
{
    return static_cast<ModulePrinterApi *>(ctx)->_queue_print(req, PRINT_JOB_TOTALIZER);
}

int32_t ModulePrinterApi::_hdl_print_sample(pal_http_request_t req, void *ctx)
{
    auto *self = static_cast<ModulePrinterApi *>(ctx);
    MsgPrintJob::Payload p{};
    p.job_id = ++self->_job_seq;
    p.kind   = PRINT_JOB_SAMPLE;
    p.source = PRINT_SRC_HTTP;
    hsys_msg_t *m = MsgPrintJob::create(self->id(), p);
    if (m) self->send(m, MODULE_PRINTER_ID, 1000);
    pal_http_resp_set_type(req, "text/plain");
    return pal_http_resp_send(req, "OK", 0);
}

// ── Maintenance endpoints ───────────────────────────────────────────────────

int32_t ModulePrinterApi::_hdl_erase_count(pal_http_request_t req, void *ctx)
{
    auto *self = static_cast<ModulePrinterApi *>(ctx);
    MsgPrinterCmd::Payload p{};
    p.cmd = PRINTER_CMD_RESET_COUNT;
    hsys_msg_t *m = MsgPrinterCmd::create(self->id(), p);
    if (m) self->send(m, MODULE_PRINTER_ID, 1000);
    pal_http_resp_set_type(req, "text/plain");
    return pal_http_resp_send(req, "OK", 0);
}

int32_t ModulePrinterApi::_hdl_device_info(pal_http_request_t req, void * /*ctx*/)
{
    char ip[20] = "0.0.0.0";
    pal_wifi_get_ip_str(ip, sizeof(ip));
    int8_t rssi = 0;
    pal_wifi_sta_get_rssi(&rssi);

    JsonDocument doc;
    char ver[48], rssi_s[8];
    snprintf(ver, sizeof(ver), "FERP-IoT-Printer (%s)", FW_VERSION);
    snprintf(rssi_s, sizeof(rssi_s), "%d", (int)rssi);
    doc["ssid"]           = app_config_get()->wifi_ssid;
    doc["ipaddress"]      = ip;
    doc["rssi"]           = rssi_s;
    doc["device_version"] = ver;
    doc["displayvalue"]   = "N/A";

    char out[256];
    serializeJson(doc, out, sizeof(out));
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, out, 0);
}

int32_t ModulePrinterApi::_hdl_thread_counter(pal_http_request_t req, void * /*ctx*/)
{
    char out[64];
    snprintf(out, sizeof(out), "{\"main_thread_counter\":\"%lu\"}",
             (unsigned long)(pal_time_get_ms() / 1000ULL));
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, out, 0);
}

int32_t ModulePrinterApi::_hdl_reboot(pal_http_request_t req, void *ctx)
{
    auto *self = static_cast<ModulePrinterApi *>(ctx);
    pal_http_resp_set_type(req, "text/plain");
    int32_t rc = pal_http_resp_send(req, "Device reboot", 0);
    LOG_MSG_WARNING(PAPI_LOG, "reboot requested over HTTP");
    hsys_task_delay(500);                  // let the response leave before the reset
    hsys_msg_t *m = MsgSystemReboot::create(self->id());
    if (m) self->publish(m);
    return rc;
}
