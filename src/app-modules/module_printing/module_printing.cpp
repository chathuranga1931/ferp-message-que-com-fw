// module_printing.cpp
//
// ModulePrinting — receipt and totalizer print client.
//
// State machine
// ─────────────
//   WAIT_CONFIG  → MsgConfigReady              → IDLE
//   IDLE         → button press (delay=0)      → HTTP
//   IDLE         → button press (delay>0)      → IDLE (timer armed)
//   IDLE         → MsgTimerAlarm               → HTTP (if delay elapsed)
//   HTTP         → MsgHttpResult (success)     → IDLE (→ _try_schedule once more)
//   HTTP         → MsgHttpResult (fail, retry) → HTTP
//   HTTP         → MsgHttpResult (fail, done)  → IDLE (→ _try_schedule once more)

#include "module_printing.h"

#include "msg_config_ready.h"
#include "msg_fuel_pumped.h"
#include "msg_totalizer_data.h"
#include "msg_printer_btn.h"
#include "msg_timer_start.h"
#include "msg_timer_alarm.h"
#include "msg_fuel_print_status.h"

// HTTP session messages (DIRECT to/from ModuleHttp)
#include "msg_http_start_request.h"
#include "msg_http_set_url_request.h"
#include "msg_http_header_request.h"
#include "msg_http_body_request.h"
#include "msg_http_send_request.h"
#include "msg_http_result.h"

#include "app.h"              // app_config_get()
#include "app_module_ids.h"
#include "pal_logger.h"
#include "pal_time.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define __TAG__      "PRINTING"
#define PRINT_LOG_EN  true

#define MLOG(...)       LOG_MSG_INFO(PRINT_LOG_EN, __VA_ARGS__)
#define MLOGE(...)      LOG_MSG_ERROR(PRINT_LOG_EN, __VA_ARGS__)

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModulePrinting s_instance;
ModulePrinting *ModulePrinting::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModulePrinting::pre_init()
{
    // Nothing needed before framework init
}

void ModulePrinting::init()
{
    subscribe(MsgConfigReady::ID);
    subscribe(MsgFuelPumped::ID);
    subscribe(MsgTotalizerData::ID);
    subscribe(MsgPrinterBtn::ID);
    subscribe(MsgTimerAlarm::ID);
    // MsgHttpResult is DIRECT — automatically delivered by HSYS

    MLOG("init");
}

// ── Message handler ───────────────────────────────────────────────────────────

void ModulePrinting::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MsgConfigReady::ID:
            _on_config_ready();
            break;

        case MsgFuelPumped::ID:
            _on_fuel_pumped(msg);
            break;

        case MsgTotalizerData::ID:
            _on_totalizer_data(msg);
            break;

        case MsgPrinterBtn::ID:
            _on_printer_btn(msg);
            break;

        case MsgTimerAlarm::ID:
            _on_timer_alarm();
            break;

        case MsgHttpResult::ID:
            _on_http_result(msg);
            break;

        default:
            break;
    }
}

// ── Config ────────────────────────────────────────────────────────────────────

void ModulePrinting::_on_config_ready()
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    strncpy(_cfg.printer_url, cfg->printer_url, sizeof(_cfg.printer_url) - 1);
    _cfg.printer_copy_count = (cfg->printer_copy_count > 0) ? cfg->printer_copy_count : 1;
    _cfg.print_delay_ms     = cfg->print_delay_ms;
    _cfg.enable_nid_print   = cfg->enable_nid_print;
    strncpy(_cfg.nozzle_ids[0], cfg->nozzle_0_id, sizeof(_cfg.nozzle_ids[0]) - 1);
    strncpy(_cfg.nozzle_ids[1], cfg->nozzle_1_id, sizeof(_cfg.nozzle_ids[1]) - 1);

    if (_state == WAIT_CONFIG) {
        _state = IDLE;
        MLOG("config ready — printer_url=%s copies=%lu delay=%lums",
             _cfg.printer_url,
             (unsigned long)_cfg.printer_copy_count,
             (unsigned long)_cfg.print_delay_ms);
    }
}

// ── Fuel pumped ───────────────────────────────────────────────────────────────

void ModulePrinting::_on_fuel_pumped(const hsys_msg_t &msg)
{
    auto p = MsgFuelPumped::deserialize(msg);
    if (p.nozzle_idx >= FUEL_MAX_NOZZLES) return;

    NozzleRecord &r  = _nozzle_rec[p.nozzle_idx];
    r.valid          = true;
    r.vol_lx1000     = p.vol_lx1000;
    r.unit_pricex100 = p.unit_pricex100;
    r.total_pricex100= p.total_pricex100;
    r.ne_id          = p.ne_id;
    r.time_stamp     = p.time_stamp;
    r.print_count    = 0;   // new transaction — reset print counter

    MLOG("nozzle[%u] pumped: vol=%lu unit=%lu total=%lu",
         (unsigned)p.nozzle_idx,
         (unsigned long)p.vol_lx1000,
         (unsigned long)p.unit_pricex100,
         (unsigned long)p.total_pricex100);
}

// ── Totalizer data ────────────────────────────────────────────────────────────

void ModulePrinting::_on_totalizer_data(const hsys_msg_t &msg)
{
    auto p = MsgTotalizerData::deserialize(msg);
    if (p.nozzle_idx >= FUEL_MAX_NOZZLES) return;

    memcpy(_totalizer_str[p.nozzle_idx], p.totalizer_str, 15);
    _totalizer_str[p.nozzle_idx][15] = '\0';
    _totalizer_valid[p.nozzle_idx]   = true;

    MLOG("nozzle[%u] totalizer: %s", (unsigned)p.nozzle_idx, _totalizer_str[p.nozzle_idx]);
}

// ── Printer button ────────────────────────────────────────────────────────────

void ModulePrinting::_on_printer_btn(const hsys_msg_t &msg)
{
    if (_state == WAIT_CONFIG) {
        MLOGE("button press ignored — config not ready");
        return;
    }

    auto p = MsgPrinterBtn::deserialize(msg);
    uint8_t nozzle_idx = (uint8_t)(p.button_id - 1);
    if (nozzle_idx >= FUEL_MAX_NOZZLES) return;

    // ── Ignore if slot already occupied ──────────────────────────────────────
    if (_pending[nozzle_idx].valid) {
        MLOG("nozzle[%u] btn ignored — pending slot occupied", (unsigned)nozzle_idx);
        return;
    }

    // ── Validate data availability ────────────────────────────────────────────
    PendingPrint pend{};
    pend.btn_type = p.status;
    pend.recv_ms  = pal_time_get_ms();

    if (p.status == BTN_SHORT_PRESS) {
        const NozzleRecord &r = _nozzle_rec[nozzle_idx];
        if (!r.valid) {
            MLOG("nozzle[%u] short-press ignored — no pump data", (unsigned)nozzle_idx);
            return;
        }
        if (r.print_count > _cfg.printer_copy_count) {
            MLOG("nozzle[%u] short-press ignored — max copies (%u) reached",
                 (unsigned)nozzle_idx, (unsigned)_cfg.printer_copy_count);
            return;
        }
        pend.vol_lx1000      = r.vol_lx1000;
        pend.unit_pricex100  = r.unit_pricex100;
        pend.total_pricex100 = r.total_pricex100;
        pend.ne_id           = r.ne_id;
        pend.time_stamp      = r.time_stamp;
        pend.copy_number     = r.print_count + 1;   // 1=original, 2=copy-1, …
    } else {   // BTN_LONG_PRESS — totalizer
        if (!_totalizer_valid[nozzle_idx]) {
            MLOG("nozzle[%u] long-press ignored — no totalizer data", (unsigned)nozzle_idx);
            return;
        }
        memcpy(pend.totalizer_str, _totalizer_str[nozzle_idx], sizeof(pend.totalizer_str));
    }

    pend.valid = true;
    _pending[nozzle_idx] = pend;

    MLOG("nozzle[%u] btn queued type=%s copy=%lu",
         (unsigned)nozzle_idx,
         (p.status == BTN_SHORT_PRESS) ? "SHORT" : "LONG",
         (unsigned long)pend.copy_number);

    if (_state != HTTP) {
        _try_schedule();
    }
}

// ── Timer alarm ───────────────────────────────────────────────────────────────

void ModulePrinting::_on_timer_alarm()
{
    if (_state != HTTP) {
        _try_schedule();
    }
}

// ── HTTP result ───────────────────────────────────────────────────────────────

void ModulePrinting::_on_http_result(const hsys_msg_t &msg)
{
    if (!_active.valid) return;

    auto f = MsgHttpResult::get_fields(msg);
    bool success = (f.result == HTTP_RESULT_SUCCESS &&
                    (f.status_code == 200 || f.status_code == 201));

    if (!success && _active.retry_count < k_max_retries) {
        _active.retry_count++;
        MLOG("nozzle[%u] HTTP failed (result=%d status=%d) retry %d/%d",
             (unsigned)_active.nozzle_idx,
             (int)f.result, (int)f.status_code,
             (int)_active.retry_count, k_max_retries);
        _send_http_request();
        return;
    }

    if (success) {
        MLOG("nozzle[%u] HTTP OK (status=%d) copy=%lu",
             (unsigned)_active.nozzle_idx,
             (int)f.status_code,
             (unsigned long)_active.copy_number);
        if (!_active.is_totalizer) {
            // Increment the print counter so the next press gets copy_number+1
            if (_active.nozzle_idx < FUEL_MAX_NOZZLES) {
                _nozzle_rec[_active.nozzle_idx].print_count++;
            }
        }
        _publish_print_status(PRINT_STATUS_OK);
    } else {
        MLOGE("nozzle[%u] HTTP failed after %d retries (result=%d status=%d)",
              (unsigned)_active.nozzle_idx, k_max_retries,
              (int)f.result, (int)f.status_code);
        _publish_print_status(PRINT_STATUS_FAILED);
    }

    _active.valid = false;
    _state        = IDLE;

    // One more scheduling cycle as requested — processes the next pending event
    _try_schedule();
}

// ── Scheduling ────────────────────────────────────────────────────────────────

void ModulePrinting::_try_schedule()
{
    if (_state == HTTP) return;
    if (!_process_one_expired()) {
        _arm_timer_for_next();
    }
}

bool ModulePrinting::_process_one_expired()
{
    uint64_t now = pal_time_get_ms();

    for (uint8_t i = 0; i < FUEL_MAX_NOZZLES; i++) {
        if (!_pending[i].valid) continue;

        uint64_t elapsed   = now - _pending[i].recv_ms;
        uint64_t delay_ms  = (uint64_t)_cfg.print_delay_ms;

        if (elapsed >= delay_ms) {
            MLOG("nozzle[%u] delay elapsed (%llums) — starting HTTP",
                 (unsigned)i, (unsigned long long)elapsed);
            // Capture into _active before clearing the slot
            _start_http_print(i, _pending[i]);
            _pending[i].valid = false;
            return true;
        }
    }
    return false;
}

void ModulePrinting::_arm_timer_for_next()
{
    if (_cfg.print_delay_ms == 0) return;   // no delay → nothing to arm

    uint64_t now          = pal_time_get_ms();
    uint32_t min_remain   = UINT32_MAX;

    for (uint8_t i = 0; i < FUEL_MAX_NOZZLES; i++) {
        if (!_pending[i].valid) continue;
        uint64_t elapsed  = now - _pending[i].recv_ms;
        uint32_t remain   = (elapsed >= (uint64_t)_cfg.print_delay_ms)
                            ? 0u
                            : (uint32_t)((uint64_t)_cfg.print_delay_ms - elapsed);
        if (remain < min_remain) min_remain = remain;
    }

    if (min_remain == UINT32_MAX) return;   // nothing pending
    if (min_remain == 0) {
        // Delay already elapsed for the nearest event — process now
        _process_one_expired();
        return;
    }

    // Clamp to 50 ms minimum to avoid busy-polling
    if (min_remain < 50) min_remain = 50;

    MsgTimerStart::Payload tp{};
    tp.source_module_id = id();
    tp.start_offset_ms  = 0;
    tp.duration_ms      = min_remain;
    tp.is_repetitive    = false;
    tp.forced           = true;
    tp.user_tag         = 0;

    hsys_msg_t *tmsg = MsgTimerStart::create(id(), tp);
    if (tmsg) publish(tmsg);

    MLOG("timer armed for %lums", (unsigned long)min_remain);
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

void ModulePrinting::_start_http_print(uint8_t nozzle_idx, const PendingPrint &pend)
{
    _active.valid           = true;
    _active.nozzle_idx      = nozzle_idx;
    _active.is_totalizer    = (pend.btn_type == BTN_LONG_PRESS);
    _active.retry_count     = 0;
    _active.copy_number     = pend.copy_number;
    _active.vol_lx1000      = pend.vol_lx1000;
    _active.unit_pricex100  = pend.unit_pricex100;
    _active.total_pricex100 = pend.total_pricex100;
    _active.ne_id           = pend.ne_id;
    _active.time_stamp      = pend.time_stamp;
    memcpy(_active.totalizer_str, pend.totalizer_str, sizeof(_active.totalizer_str));

    _state = HTTP;
    _send_http_request();
}

void ModulePrinting::_send_http_request()
{
    // 1. Open session
    {
        MsgHttpStartRequest::Payload sp{};
        sp.method     = PAL_HTTP_METHOD_POST;
        sp.timeout_ms = 8000;
        hsys_msg_t *m = MsgHttpStartRequest::create(id(), sp);
        if (m) send(m, MODULE_HTTP_ID);
    }

    // 2. URL
    {
        char url[sizeof(_cfg.printer_url) + 12];
        if (_active.is_totalizer) {
            snprintf(url, sizeof(url), "%s-totalizer", _cfg.printer_url);
        } else {
            strncpy(url, _cfg.printer_url, sizeof(url) - 1);
            url[sizeof(url) - 1] = '\0';
        }
        hsys_msg_t *m = MsgHttpSetUrlRequest::create(id(), url);
        if (m) send(m, MODULE_HTTP_ID);
    }

    // 3. Content-Type header
    {
        hsys_msg_t *m = MsgHttpHeaderRequest::create(id(), "Content-Type", "application/json");
        if (m) send(m, MODULE_HTTP_ID);
    }

    // 4. Body
    {
        char body[512];
        _build_body(body, sizeof(body));
        uint32_t len = (uint32_t)strnlen(body, sizeof(body));
        hsys_msg_t *m = MsgHttpBodyRequest::create(id(), body, len);
        if (m) send(m, MODULE_HTTP_ID);
    }

    // 5. Fire
    {
        hsys_msg_t *m = MsgHttpSendRequest::create(id());
        if (m) send(m, MODULE_HTTP_ID);
    }

    MLOG("nozzle[%u] HTTP %s sent (retry=%d)",
         (unsigned)_active.nozzle_idx,
         _active.is_totalizer ? "totalizer" : "receipt",
         (int)_active.retry_count);
}

void ModulePrinting::_build_body(char *buf, uint32_t buf_len) const
{
    const char *nozzle_id = (_active.nozzle_idx < FUEL_MAX_NOZZLES)
                            ? _cfg.nozzle_ids[_active.nozzle_idx]
                            : "?";

    if (_active.is_totalizer) {
        // ── Totalizer body ────────────────────────────────────────────────────
        StaticJsonDocument<256> doc;
        doc["time"]       = _active.time_stamp;
        doc["nozzel_id"]  = nozzle_id;
        doc["print_note"] = "Totalizer";
        JsonObject meas   = doc.createNestedObject("measurements");
        char safe_tot[17];
        memcpy(safe_tot, _active.totalizer_str, 16);
        safe_tot[16] = '\0';
        meas["L"]     = safe_tot;
        serializeJson(doc, buf, buf_len);
    } else {
        // ── Receipt body ──────────────────────────────────────────────────────
        char note[24];
        if (_active.copy_number == 1) {
            strncpy(note, "Original", sizeof(note) - 1);
            note[sizeof(note) - 1] = '\0';
        } else {
            snprintf(note, sizeof(note), "Copy - %u", (unsigned)(_active.copy_number - 1));
        }

        float vol_l     = (float)_active.vol_lx1000    / 1000.0f;
        float total_p   = (float)_active.total_pricex100 / 100.0f;
        float unit_p    = (float)_active.unit_pricex100  / 100.0f;

        StaticJsonDocument<512> doc;
        doc["time"]        = _active.time_stamp;
        doc["print_time"]  = _active.time_stamp;
        doc["nozzel_id"]   = nozzle_id;
        doc["print_note"]  = note;

        JsonObject meas = doc.createNestedObject("measurements");
        meas["L"] = vol_l;
        meas["T"] = "";
        meas["P"] = total_p;
        meas["U"] = unit_p;

        if (_cfg.enable_nid_print) {
            char ne_id_str[24];
            snprintf(ne_id_str, sizeof(ne_id_str), "%llu",
                     (unsigned long long)_active.ne_id);
            meas["NE_ID"]  = ne_id_str;
            meas["ABS_ID"] = _active.ne_id;
        }

        serializeJson(doc, buf, buf_len);
    }
}

void ModulePrinting::_publish_print_status(print_status_t status)
{
    MsgFuelPrintStatus::Payload p{};
    p.nozzle_idx         = _active.nozzle_idx;
    p.status             = status;
    p.dispenser_event_id = (uint32_t)(_active.ne_id & 0xFFFFFFFFULL);
    p.timestamp_epoch    = (int64_t)_active.time_stamp;
    p.ne_id              = _active.ne_id;

    hsys_msg_t *msg = MsgFuelPrintStatus::create(id(), p);
    if (msg) publish(msg);
}
