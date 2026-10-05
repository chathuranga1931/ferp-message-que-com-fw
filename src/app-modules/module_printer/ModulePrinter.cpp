// ModulePrinter.cpp — see ModulePrinter.h

#include "ModulePrinter.h"

#include "app.h"                 // app_config_get()
#include "app_config.h"
#include "app_device_info.h"
#include "app_spiffs.h"
#include "version.h"

#include "msg_config_ready.h"
#include "msg_wifi_event.h"
#include "msg_print_job.h"
#include "msg_print_result.h"
#include "msg_printer_cmd.h"
#include "msg_printer_get_status.h"
#include "msg_printer_status.h"

#include "pal_gpio.h"
#include "pal_logger.h"
#include "hsys_task.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __TAG__      "PRINTER "
#define PRN_LOG      true

static constexpr const char *k_count_file = "Logs/stats.json";   // legacy location

static ModulePrinter s_instance;
ModulePrinter *ModulePrinter::instance() { return &s_instance; }

// ── Lifecycle ───────────────────────────────────────────────────────────────

void ModulePrinter::init()
{
    subscribe(MSG_ID_CONFIG_READY);
    subscribe(MSG_ID_WIFI_EVENT);
    subscribe(MSG_ID_PRINT_JOB);
    subscribe(MSG_ID_PRINTER_CMD);
    subscribe(MSG_ID_PRINTER_GET_STATUS);

    if (_buzzer_gpio >= 0) {
        pal_gpio_config_t cfg = PAL_GPIO_CFG_OUTPUT();
        pal_gpio_config((pal_gpio_num_t)_buzzer_gpio, cfg);
        pal_gpio_set_level((pal_gpio_num_t)_buzzer_gpio, PAL_GPIO_LEVEL_LOW);
    }
    if (!_transport) {
        LOG_MSG_ERROR(PRN_LOG, "no transport configured — printing disabled");
    }
    LOG_MSG_INFO(PRN_LOG, "init — transport=%s", _transport ? _transport->name() : "none");
}

void ModulePrinter::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MSG_ID_CONFIG_READY:
            _on_config_ready();
            break;

        case MSG_ID_WIFI_EVENT: {
            auto p = MsgWifiEvent::deserialize(msg);
            if (p.event != WIFI_EVENT_STA_GOT_IP) break;
            strncpy(_ip, p.ip_address, sizeof(_ip) - 1);
            const app_config_t *cfg = app_config_get();
            if (!_info_printed && cfg->prn_en_boot_print) {
                _info_printed = true;
                receipt_job_t none{};
                _print(none, PRINT_JOB_INFO);
            }
            break;
        }

        case MSG_ID_PRINT_JOB:
            _on_print_job(msg);
            break;

        case MSG_ID_PRINTER_CMD:
            _on_cmd(msg);
            break;

        case MSG_ID_PRINTER_GET_STATUS:
            _send_status(msg.sender_id, 0xFF);
            break;

        default:
            break;
    }
}

// ── Config ──────────────────────────────────────────────────────────────────

void ModulePrinter::_on_config_ready()
{
    if (!_count_loaded) {
        _load_count();
        _count_loaded = true;
    }
    if (_transport) {
        const app_config_t *cfg = app_config_get();
        _transport->begin(cfg->prn_baud_rate);
    }
}

void ModulePrinter::_fill_cfg(receipt_cfg_t &c) const
{
    const app_config_t *cfg = app_config_get();
    c.name_l1         = cfg->prn_name_l1;
    c.name_l2         = cfg->prn_name_l2;
    c.add_l1          = cfg->prn_add_l1;
    c.add_l2          = cfg->prn_add_l2;
    c.tele            = cfg->prn_tele;
    c.thank[0]        = cfg->prn_thank_l1;
    c.thank[1]        = cfg->prn_thank_l2;
    c.thank[2]        = cfg->prn_thank_l3;
    c.thank[3]        = cfg->prn_thank_l4;
    c.thank[4]        = cfg->prn_thank_l5;
    c.thank[5]        = cfg->prn_thank_l6;
    c.footer          = cfg->prn_footer;
    c.theme           = cfg->prn_theme;
    c.printer_dots    = cfg->prn_printer_dots;
    c.end_lines       = cfg->prn_end_lines;
    c.lines_after_cut = cfg->prn_lines_after_cut;
    c.en_print_time   = cfg->prn_en_print_time;
    c.en_signature    = cfg->prn_en_signature;
    c.en_cut          = cfg->prn_en_cut;
}

// ── Jobs ────────────────────────────────────────────────────────────────────

void ModulePrinter::_on_print_job(const hsys_msg_t &msg)
{
    MsgPrintJob::Payload p = MsgPrintJob::deserialize(msg);

    receipt_job_t job = (p.kind == PRINT_JOB_SAMPLE) ? ReceiptRenderer::sample_job() : receipt_job_t{};
    if (p.kind != PRINT_JOB_SAMPLE) {
        job.time_stamp   = p.time_stamp;
        job.print_time   = p.print_time;
        job.print_note   = p.print_note;
        job.nozzle_id    = p.nozzle_id;
        job.fuel_type    = p.fuel_type;
        job.event_id     = p.event_id;
        job.totalizer    = p.totalizer;
        job.has_event_id = p.has_event_id;
        job.volume_l     = p.volume_l;
        job.unit_price   = p.unit_price;
        job.total_price  = p.total_price;
    }

    LOG_MSG_INFO(PRN_LOG, "job id=%lu kind=%u src=%u nozzle='%s' L=%.3f P=%.2f note='%s' bill='%s'",
                 (unsigned long)p.job_id, p.kind, p.source, p.nozzle_id, p.volume_l,
                 p.total_price, p.print_note, p.event_id);

    print_result_t r = (p.kind <= PRINT_JOB_INFO) ? _print(job, p.kind) : PRINT_RESULT_INVALID;
    _last_job_id = p.job_id;

    if (msg.sender_id != HSYS_MODULE_ID_INVALID && msg.sender_id != id()) {
        MsgPrintResult::Payload rp{};
        rp.job_id      = p.job_id;
        rp.result      = (uint8_t)r;
        rp.source      = p.source;
        rp.print_count = _print_count;
        hsys_msg_t *out = MsgPrintResult::create(id(), rp);
        if (out) send(out, msg.sender_id);
    }
}

print_result_t ModulePrinter::_print(const receipt_job_t &job, uint8_t kind)
{
    receipt_cfg_t cfg{};
    _fill_cfg(cfg);
    ReceiptRenderer r(_buf, sizeof(_buf));
    size_t n = 0;

    switch (kind) {
        case PRINT_JOB_TOTALIZER:
            n = r.render_totalizer(cfg, job);
            break;
        case PRINT_JOB_INFO: {
            const app_device_info_t *di = app_device_info_get();
            char mac[18] = {};
            const char *h = di->hw_address;
            if (strlen(h) == 12) {
                snprintf(mac, sizeof(mac), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                         h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]);
                for (char *c = mac; *c; c++) if (*c >= 'a' && *c <= 'f') *c = (char)(*c - 32);
            }
            n = r.render_info(cfg, mac, _ip[0] ? _ip : "0.0.0.0", FW_VERSION, _print_count);
            break;
        }
        default:                          // receipt + sample
            // Legacy behaviour: the counter is bumped before the bill is printed
            // and that number is the bill number when the job carries none.
            _print_count++;
            _save_count();
            n = r.render_receipt(cfg, job, _print_count);
            break;
    }

    print_result_t res = PRINT_RESULT_OK;
    if (n == 0) {
        LOG_MSG_ERROR(PRN_LOG, "render failed (buffer overflow)");
        res = PRINT_RESULT_INVALID;
    } else if (!_transport || !_transport->is_ready()) {
        LOG_MSG_ERROR(PRN_LOG, "printer not ready (%s)", _transport ? _transport->name() : "none");
        res = PRINT_RESULT_NOT_READY;
    } else if (!_transport->write(_buf, n, WRITE_TIMEOUT_MS)) {
        res = PRINT_RESULT_WRITE_FAIL;
    }

    _last_result = (uint8_t)res;
    if (res == PRINT_RESULT_OK) {
        _jobs_ok++;
        LOG_MSG_INFO(PRN_LOG, "printed %u bytes (kind=%u, bill count=%lu)",
                     (unsigned)n, kind, (unsigned long)_print_count);
    } else {
        _jobs_failed++;
        LOG_MSG_ERROR(PRN_LOG, "print failed result=%d kind=%u", (int)res, kind);
        if (kind != PRINT_JOB_INFO) _beep(2);
    }
    return res;
}

// ── Commands / status ───────────────────────────────────────────────────────

void ModulePrinter::_on_cmd(const hsys_msg_t &msg)
{
    auto p = MsgPrinterCmd::deserialize(msg);
    LOG_MSG_INFO(PRN_LOG, "cmd %u arg %u", p.cmd, p.arg);
    switch (p.cmd) {
        case PRINTER_CMD_PRINT_SAMPLE:
            _print(ReceiptRenderer::sample_job(), PRINT_JOB_SAMPLE);
            break;
        case PRINTER_CMD_RESET_COUNT:
            // Legacy bug fix: the old /erasePrintCount only rewrote the file,
            // the in-RAM counter kept counting from the old value.
            _print_count = 0;
            _save_count();
            break;
        case PRINTER_CMD_PRINT_INFO: {
            receipt_job_t none{};
            _print(none, PRINT_JOB_INFO);
            break;
        }
        case PRINTER_CMD_BEEP:
            _beep(p.arg ? p.arg : 1);
            break;
        default:
            LOG_MSG_WARNING(PRN_LOG, "unknown cmd %u", p.cmd);
            break;
    }
    if (msg.sender_id != HSYS_MODULE_ID_INVALID && msg.sender_id != id())
        _send_status(msg.sender_id, p.cmd);
}

void ModulePrinter::_send_status(hsys_module_id_t to, uint8_t last_cmd)
{
    if (to == HSYS_MODULE_ID_INVALID) return;
    MsgPrinterStatus::Payload s{};
    strncpy(s.transport, _transport ? _transport->name() : "none", sizeof(s.transport) - 1);
    s.ready       = _transport && _transport->is_ready();
    s.last_result = _last_result;
    s.last_cmd    = last_cmd;
    s.print_count = _print_count;
    s.jobs_ok     = _jobs_ok;
    s.jobs_failed = _jobs_failed;
    s.last_job_id = _last_job_id;
    if (_transport) _transport->detail(s.detail, sizeof(s.detail));
    hsys_msg_t *out = MsgPrinterStatus::create(id(), s);
    if (out) send(out, to);
}

// ── Bill counter persistence ────────────────────────────────────────────────

void ModulePrinter::_load_count()
{
    char buf[128] = {};
    size_t got = 0;
    _print_count = 0;
    if (app_spiffs_read_file(k_count_file, buf, sizeof(buf) - 1, &got, 2000) == APP_SPIFFS_OK && got > 0) {
        buf[got] = '\0';
        JsonDocument doc;
        if (deserializeJson(doc, buf) == DeserializationError::Ok) {
            JsonVariantConst v = doc["print_count"];
            if (v.is<const char *>()) _print_count = (uint32_t)strtoul(v.as<const char *>(), nullptr, 10);
            else                      _print_count = v | (uint32_t)0;
        }
        LOG_MSG_INFO(PRN_LOG, "bill count loaded: %lu", (unsigned long)_print_count);
    } else {
        LOG_MSG_WARNING(PRN_LOG, "no %s — bill count starts at 0", k_count_file);
        _save_count();
    }
}

void ModulePrinter::_save_count()
{
    char buf[48];
    // Same shape as the legacy firmware: the count is stored as a string.
    snprintf(buf, sizeof(buf), "{\"print_count\":\"%lu\"}", (unsigned long)_print_count);
    // Braces required: LOG_MSG_* expands to two statements.
    if (app_spiffs_write_file(k_count_file, buf, 2000) != APP_SPIFFS_OK) {
        LOG_MSG_WARNING(PRN_LOG, "bill count save failed");
    }
}

// ── Buzzer ──────────────────────────────────────────────────────────────────

void ModulePrinter::_beep(uint32_t seconds)
{
    if (_buzzer_gpio < 0) return;
    if (seconds > 10) seconds = 10;
    // Legacy pattern: toggle every 250 ms while beeping.
    for (uint32_t i = 0; i < seconds * 4; i++) {
        pal_gpio_set_level((pal_gpio_num_t)_buzzer_gpio, (i & 1) ? PAL_GPIO_LEVEL_LOW : PAL_GPIO_LEVEL_HIGH);
        hsys_task_delay(250);
    }
    pal_gpio_set_level((pal_gpio_num_t)_buzzer_gpio, PAL_GPIO_LEVEL_LOW);
}
