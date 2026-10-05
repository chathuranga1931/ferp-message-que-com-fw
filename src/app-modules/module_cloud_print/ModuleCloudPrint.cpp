// ModuleCloudPrint.cpp — see ModuleCloudPrint.h

#include "ModuleCloudPrint.h"

#include "app.h"
#include "app_config.h"
#include "app_device_info.h"

#include "msg_config_ready.h"
#include "msg_internet_status.h"
#include "msg_http_request.h"
#include "msg_http_result.h"
#include "msg_http_response_header.h"
#include "msg_mqtt_subscribe.h"
#include "msg_mqtt_ext_data.h"
#include "msg_print_job.h"
#include "msg_print_result.h"
#include "msg_cloud_print_get_status.h"
#include "msg_cloud_print_status.h"
#include "msg_dev_info_write.h"

#include "pal_crypto.h"
#include "pal_efuse.h"
#include "pal_logger.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define __TAG__   "CLOUDPRN"
#define CP_LOG    true

// Same backend and SAS-AC1 secret as ModuleCubeSphere / the legacy printer.
#define CP_BASE_URL        "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app"
#define CP_URL_BOOTSTRAP   CP_BASE_URL "/api/bootstrap/core/v1/device"
#define CP_URL_DEV_CONFIG  CP_BASE_URL "/api/ingress/core/v1/device/config"
#define CP_URL_QUEUE       CP_BASE_URL "/api/ingress/ext.queue/v1/queue/"
#define CP_TIMEZONE        "Asia/Colombo"
static const char k_sas_key[] = "y4M5oJVfjAWeN059p";

static constexpr uint32_t k_http_timeout_ms  = 30000;
static constexpr uint32_t k_http_stuck_s     = 75;    // ModuleHttp never answered → reset
static constexpr uint32_t k_reg_retry_s      = 60;
static constexpr uint32_t k_print_retry_s    = 30;    // printer failed → retry the same message
static constexpr uint32_t k_poll_error_s     = 30;
static constexpr uint32_t k_default_poll_s   = 60;

static ModuleCloudPrint s_instance;
ModuleCloudPrint *ModuleCloudPrint::instance() { return &s_instance; }

// ── Lifecycle ───────────────────────────────────────────────────────────────

void ModuleCloudPrint::init()
{
    subscribe(MSG_ID_CONFIG_READY);
    subscribe(MSG_ID_INTERNET_STATUS);
    subscribe(MSG_ID_TICK_1000MS);
    subscribe(MSG_ID_HTTP_RESULT);
    subscribe(MSG_ID_HTTP_RESPONSE_HEADER);
    subscribe(MSG_ID_MQTT_EXT_DATA);
    subscribe(MSG_ID_PRINT_RESULT);
    subscribe(MSG_ID_CLOUD_PRINT_GET_STATUS);
    LOG_MSG_INFO(CP_LOG, "init");
}

void ModuleCloudPrint::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MSG_ID_CONFIG_READY:          _on_config_ready();                                 break;
        case MSG_ID_INTERNET_STATUS:       _on_internet(MsgInternetStatus::deserialize(msg).connected); break;
        case MSG_ID_TICK_1000MS:           _on_tick();                                         break;
        case MSG_ID_HTTP_RESPONSE_HEADER:  _on_http_header(msg);                               break;
        case MSG_ID_HTTP_RESULT:           _on_http_result(msg);                               break;
        case MSG_ID_PRINT_RESULT:          _on_print_result(msg);                              break;
        case MSG_ID_MQTT_EXT_DATA:         _on_notify();                                       break;
        case MSG_ID_CLOUD_PRINT_GET_STATUS: _send_status(msg.sender_id);                       break;
        default: break;
    }
}

void ModuleCloudPrint::_set_state(uint8_t s)
{
    if (_state == s) return;
    _state = s;
    LOG_MSG_INFO(CP_LOG, "state → %u", (unsigned)s);
}

void ModuleCloudPrint::_on_config_ready()
{
    bool was = _enabled;
    _enabled = app_config_get()->prn_en_cloud_print;
    if (!_enabled) {
        if (was) LOG_MSG_INFO(CP_LOG, "cloud print disabled");
        _set_state(CLOUD_PRINT_DISABLED);
        // An in-flight HTTP request still completes; its result is ignored.
        _step = STEP_IDLE;
        _wait_s = 0;
        return;
    }
    if (!was) {
        LOG_MSG_INFO(CP_LOG, "cloud print enabled");
        _set_state(CLOUD_PRINT_WAIT_NET);
        if (_internet) _start_registration();
    }
}

void ModuleCloudPrint::_on_internet(bool up)
{
    bool was = _internet;
    _internet = up;
    if (!_enabled || !up || was) return;
    // (Re)connected: register if not done yet, otherwise check the queue.
    if (_queue_id[0] == '\0') {
        if (_step == STEP_IDLE) _start_registration();
    } else {
        _poll_pending = true;
    }
}

// ── 1 s tick: timers, fallback poll, stuck-request guard ────────────────────

void ModuleCloudPrint::_on_tick()
{
    if (!_enabled) return;

    if (_http_busy && ++_http_age_s > k_http_stuck_s) {
        LOG_MSG_ERROR(CP_LOG, "HTTP request stuck (step %u) — resetting", (unsigned)_step);
        _http_busy = false;
        if (_step == STEP_REG_1 || _step == STEP_REG_2 || _step == STEP_REG_3) _reg_failed("http stuck");
        else _idle(k_poll_error_s);
        return;
    }

    if (_wait_s > 0 && --_wait_s == 0) {
        switch (_step) {
            case STEP_REG_WAIT_2: _reg_step_2(); return;
            case STEP_IDLE:
                if (_queue_id[0] == '\0') { if (_internet) _start_registration(); }
                else _poll_pending = true;
                break;
            default: break;
        }
    }

    _poll_age_s++;
    if (_step != STEP_IDLE || _http_busy || !_internet || _queue_id[0] == '\0' || _wait_s > 0) return;

    uint32_t poll_s = app_config_get()->prn_cp_poll_s;
    if (poll_s > 0 && poll_s < 10) poll_s = 10;
    if (_poll_pending || (poll_s > 0 && _poll_age_s >= poll_s)) {
        _poll_pending = false;
        _poll();
    }
}

void ModuleCloudPrint::_on_notify()
{
    if (!_enabled) return;
    LOG_MSG_INFO(CP_LOG, "queue notification received");
    if (_step == STEP_IDLE && !_http_busy && _queue_id[0] != '\0') {
        _wait_s = 0;
        _poll();
    } else {
        _poll_pending = true;   // handled when the current job is finished
    }
}

void ModuleCloudPrint::_idle(uint32_t wait_s)
{
    _step   = STEP_IDLE;
    _wait_s = wait_s;
}

// ── HTTP helper ─────────────────────────────────────────────────────────────

bool ModuleCloudPrint::_http(pal_http_method_t method, const char *url, const char *auth,
                             const char *body, const char *collect_key)
{
    static char hdrs[512];
    uint16_t hl = 0;
    auto add = [&](const char *k, const char *v) {
        size_t kl = strlen(k) + 1, vl = strlen(v) + 1;
        if (hl + kl + vl > sizeof(hdrs)) return;
        memcpy(hdrs + hl, k, kl); hl += (uint16_t)kl;
        memcpy(hdrs + hl, v, vl); hl += (uint16_t)vl;
    };
    if (auth && auth[0]) add("Authorization", auth);
    if (body) add("Content-Type", "application/json");

    hsys_msg_t *m = MsgHttpRequest::create(id(), method, k_http_timeout_ms, _root_ca, url,
                                           hl ? hdrs : nullptr, hl,
                                           body, body ? (uint16_t)strlen(body) : 0,
                                           collect_key);
    if (!m) {
        LOG_MSG_ERROR(CP_LOG, "MsgHttpRequest alloc failed");
        return false;
    }
    _http_busy  = true;
    _http_age_s = 0;
    send(m, MODULE_HTTP_ID);
    return true;
}

void ModuleCloudPrint::_on_http_header(const hsys_msg_t &msg)
{
    if (_step != STEP_REG_1) return;
    const char *key = MsgHttpResponseHeader::get_key(msg);
    const char *val = MsgHttpResponseHeader::get_value(msg);
    if (!key || !val || strcasecmp(key, "www-authenticate") != 0) return;
    const char *ns = strstr(val, "nonce=\"");
    if (!ns) return;
    ns += 7;
    const char *ne = strchr(ns, '"');
    if (!ne || (size_t)(ne - ns) >= sizeof(_nonce)) {
        LOG_MSG_ERROR(CP_LOG, "nonce too long (%u chars)", (unsigned)(ne ? ne - ns : 0));
        return;
    }
    memcpy(_nonce, ns, (size_t)(ne - ns));
    _nonce[ne - ns] = '\0';
}

void ModuleCloudPrint::_on_http_result(const hsys_msg_t &msg)
{
    if (!_http_busy) return;
    _http_busy = false;
    auto f = MsgHttpResult::get_fields(msg);

    if (f.result == HTTP_RESULT_BUSY) {
        // When the 2 s wait expires the tick restarts registration (no queue
        // id yet) or polls again (an un-ACKed message comes back and is
        // re-ACKed without reprinting).
        LOG_MSG_WARNING(CP_LOG, "ModuleHttp busy — retry in 2 s");
        if (_step == STEP_REG_1 || _step == STEP_REG_2 || _step == STEP_REG_3) _queue_id[0] = '\0';
        _idle(2);
        return;
    }
    if (!_enabled) { _idle(); return; }

    int32_t status = (f.result == HTTP_RESULT_SUCCESS) ? f.status_code : 0;
    _last_http = status;
    const char *body = (const char *)f.body;
    uint32_t len = f.body_len;

    switch (_step) {
        case STEP_REG_1: _handle_reg_1(status);            break;
        case STEP_REG_2: _handle_reg_2(status, body, len); break;
        case STEP_REG_3: _handle_reg_3(status, body, len); break;
        case STEP_POLL:  _handle_poll(status, body, len);  break;
        case STEP_ACK:   _handle_ack(status);              break;
        default: break;
    }
}

// ── Registration ────────────────────────────────────────────────────────────

void ModuleCloudPrint::_start_registration()
{
    if (!_enabled || !_internet || _http_busy) return;
    _set_state(CLOUD_PRINT_REGISTERING);
    _nonce[0] = _basic_b64[0] = _queue_id[0] = '\0';
    _reg_step_1();
}

void ModuleCloudPrint::_reg_step_1()
{
    LOG_MSG_INFO(CP_LOG, "register 1/3 — bootstrap (expect 401 + nonce)");
    _step = STEP_REG_1;
    if (!_http(PAL_HTTP_METHOD_GET, CP_URL_BOOTSTRAP, nullptr, nullptr, "www-authenticate"))
        _reg_failed("alloc");
}

void ModuleCloudPrint::_handle_reg_1(int32_t status)
{
    if (status != 401 || _nonce[0] == '\0') {
        _reg_failed(status != 401 ? "bootstrap: expected 401" : "bootstrap: no nonce");
        return;
    }
    _step   = STEP_REG_WAIT_2;
    _wait_s = 1;
}

static void _sha256_hex(const char *in, char *out, size_t out_len)
{
    uint8_t h[PAL_SHA256_DIGEST_LENGTH] = {};
    pal_crypto_sha256((const uint8_t *)in, strlen(in), h);
    pal_crypto_bin_to_hex(h, sizeof(h), out, out_len);
}

void ModuleCloudPrint::_reg_step_2()
{
    uint8_t mac[6] = {};
    char mac12[13] = {};
    if (pal_efuse_get_mac(mac, sizeof(mac)) == PAL_OK)
        snprintf(mac12, sizeof(mac12), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // token = sha256( sha256(MAC) + key + nonce )   (hex strings)
    char sha_mac[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {};
    _sha256_hex(mac12, sha_mac, sizeof(sha_mac));
    char input[PAL_SHA256_DIGEST_LENGTH * 2 + sizeof(k_sas_key) + sizeof(_nonce)] = {};
    snprintf(input, sizeof(input), "%s%s%s", sha_mac, k_sas_key, _nonce);
    char token[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {};
    _sha256_hex(input, token, sizeof(token));

    snprintf(_auth, sizeof(_auth), "SAS-AC1 nonce=\"%s\" id=\"%s\" token=\"%s\"", _nonce, mac12, token);
    LOG_MSG_INFO(CP_LOG, "register 2/3 — bootstrap with SAS-AC1 (id %s)", mac12);
    _step = STEP_REG_2;
    if (!_http(PAL_HTTP_METHOD_GET, CP_URL_BOOTSTRAP, _auth, nullptr, nullptr))
        _reg_failed("alloc");
}

void ModuleCloudPrint::_handle_reg_2(int32_t status, const char *body, uint32_t len)
{
    if ((status != 200 && status != 201) || !body || len == 0) { _reg_failed("bootstrap auth"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body, len) != DeserializationError::Ok) { _reg_failed("bootstrap json"); return; }
    const char *dev_id = doc["data"]["device_id"] | (const char *)nullptr;
    const char *secret = doc["data"]["secret"]    | (const char *)nullptr;
    if (!dev_id || !secret) { _reg_failed("bootstrap: no device_id/secret"); return; }

    char id_secret[160];
    snprintf(id_secret, sizeof(id_secret), "%s:%s", dev_id, secret);
    memset(_basic_b64, 0, sizeof(_basic_b64));
    pal_crypto_base64_encode((const uint8_t *)id_secret, strlen(id_secret), _basic_b64, sizeof(_basic_b64));
    strncpy(_device_id, dev_id, sizeof(_device_id) - 1);

    // Publish the cloud identity so ModuleMqtt adds the UUID topics and the
    // device info shows it (this module is the device-UUID writer on printers).
    hsys_msg_t *w = MsgDevInfoWrite::create_str(id(), DEV_INFO_KEY_DEVICE_UUID, _device_id);
    if (w) publish(w);

    _reg_step_3();
}

void ModuleCloudPrint::_reg_step_3()
{
    snprintf(_auth, sizeof(_auth), "Basic %s", _basic_b64);
    LOG_MSG_INFO(CP_LOG, "register 3/3 — device config");
    _step = STEP_REG_3;
    if (!_http(PAL_HTTP_METHOD_GET, CP_URL_DEV_CONFIG, _auth, nullptr, nullptr))
        _reg_failed("alloc");
}

void ModuleCloudPrint::_handle_reg_3(int32_t status, const char *body, uint32_t len)
{
    if ((status != 200 && status != 201) || !body || len == 0) { _reg_failed("device config"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body, len, DeserializationOption::NestingLimit(20)) != DeserializationError::Ok) {
        _reg_failed("device config json");
        return;
    }
    const char *qid   = doc["data"]["printer-queue-id"]     | (const char *)nullptr;
    const char *topic = doc["data"]["printer-notify-topic"] | (const char *)nullptr;
    if (!qid || !qid[0]) {
        LOG_MSG_ERROR(CP_LOG, "device config has no printer-queue-id — is this device set up as a printer in the cloud?");
        _set_state(CLOUD_PRINT_NO_QUEUE);
        _idle(k_reg_retry_s * 5);
        return;
    }
    strncpy(_queue_id, qid, sizeof(_queue_id) - 1);
    _queue_id[sizeof(_queue_id) - 1] = '\0';
    LOG_MSG_INFO(CP_LOG, "registered — queue '%s' notify '%s'", _queue_id, topic ? topic : "(none)");

    if (topic && topic[0] && strcmp(topic, _notify_topic) != 0) {
        if (_notify_topic[0]) {   // topic changed → drop the old subscription
            MsgMqttSubscribe::Payload u{};
            strncpy(u.topic, _notify_topic, sizeof(u.topic) - 1);
            u.unsubscribe = true;
            hsys_msg_t *um = MsgMqttSubscribe::create(id(), u);
            if (um) send(um, MODULE_MQTT_ID);
        }
        strncpy(_notify_topic, topic, sizeof(_notify_topic) - 1);
        _notify_topic[sizeof(_notify_topic) - 1] = '\0';
        MsgMqttSubscribe::Payload sp{};
        strncpy(sp.topic, _notify_topic, sizeof(sp.topic) - 1);
        sp.qos = 1;
        hsys_msg_t *sm = MsgMqttSubscribe::create(id(), sp);
        if (sm) {
            send(sm, MODULE_MQTT_ID);
            _notify_subscribed = true;
        }
    }
    _set_state(CLOUD_PRINT_READY);
    // Legacy behaviour: check the queue right after start-up.
    _idle();
    _poll_pending = true;
}

void ModuleCloudPrint::_reg_failed(const char *why)
{
    LOG_MSG_ERROR(CP_LOG, "registration failed (%s, http %ld) — retry in %lus",
                  why, (long)_last_http, (unsigned long)k_reg_retry_s);
    _set_state(CLOUD_PRINT_REG_FAILED);
    _queue_id[0] = '\0';
    _idle(k_reg_retry_s);
}

// ── Queue poll / print / ACK ────────────────────────────────────────────────

void ModuleCloudPrint::_poll()
{
    static char url[256];
    snprintf(url, sizeof(url), CP_URL_QUEUE "%s/messages?timezone=" CP_TIMEZONE, _queue_id);
    snprintf(_auth, sizeof(_auth), "Basic %s", _basic_b64);
    _poll_age_s = 0;
    _polls++;
    _step = STEP_POLL;
    if (!_http(PAL_HTTP_METHOD_GET, url, _auth, nullptr, nullptr)) _idle(k_poll_error_s);
}

static void _cp_str(char *dst, size_t n, JsonVariantConst v)
{
    dst[0] = '\0';
    if (v.is<const char *>()) { strncpy(dst, v.as<const char *>(), n - 1); dst[n - 1] = '\0'; }
    else if (v.is<long long>()) snprintf(dst, n, "%lld", v.as<long long>());
    else if (v.is<double>())    snprintf(dst, n, "%g", v.as<double>());
}

static double _cp_num(JsonVariantConst v)
{
    if (v.is<const char *>()) return strtod(v.as<const char *>(), nullptr);
    return v | 0.0;
}

void ModuleCloudPrint::_handle_poll(int32_t status, const char *body, uint32_t len)
{
    if (status == 401 || status == 403) {
        LOG_MSG_WARNING(CP_LOG, "queue poll unauthorised (%ld) — re-registering", (long)status);
        _queue_id[0] = '\0';
        _idle(2);
        return;
    }
    if (status == 204 || status == 404) { _idle(); return; }      // nothing queued
    // The queue service answers an empty queue with
    // 400 {"error":{"key":"EMPTY_QUEUE_ERROR",...}} — not an error for us.
    if (status == 400 && body && len && memmem(body, len, "EMPTY_QUEUE", 11)) { _idle(); return; }
    if ((status != 200 && status != 201) || !body || len == 0) {
        LOG_MSG_WARNING(CP_LOG, "queue poll failed (http %ld): %.*s", (long)status,
                        (int)(len > 160 ? 160 : len), body ? body : "");
        _idle(k_poll_error_s);
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body, len, DeserializationOption::NestingLimit(20)) != DeserializationError::Ok) {
        LOG_MSG_WARNING(CP_LOG, "queue poll: unparsable body (%u B)", (unsigned)len);
        _idle(k_poll_error_s);
        return;
    }
    JsonVariantConst data    = doc["data"];
    JsonVariantConst message = data["message"];
    _has_next = data["has_next"] | false;
    const char *mid = message["message_id"] | (const char *)nullptr;
    if (!mid || !mid[0]) { _idle(); return; }                      // queue empty
    strncpy(_msg_id, mid, sizeof(_msg_id) - 1);
    _msg_id[sizeof(_msg_id) - 1] = '\0';

    // Printed earlier but the ACK did not get through → ACK again, no reprint.
    if (strcmp(_msg_id, _last_printed) == 0) {
        LOG_MSG_WARNING(CP_LOG, "message %s already printed — re-sending ACK", _msg_id);
        _send_ack(true);
        return;
    }

    JsonVariantConst pd = message["data"];
    if (!pd["pump_time"].is<const char *>()) {
        LOG_MSG_ERROR(CP_LOG, "message %s has no pump_time — REJECTED", _msg_id);
        _rejected++;
        _send_ack(false);
        return;
    }

    MsgPrintJob::Payload p{};
    p.job_id      = ++_job_seq;
    p.kind        = PRINT_JOB_RECEIPT;
    p.source      = PRINT_SRC_CLOUD;
    p.volume_l    = _cp_num(pd["L"]);
    p.unit_price  = _cp_num(pd["U"]);
    p.total_price = _cp_num(pd["P"]);
    _cp_str(p.time_stamp, sizeof(p.time_stamp), pd["pump_time"]);
    _cp_str(p.print_time, sizeof(p.print_time), pd["print_requested_time"]);
    _cp_str(p.print_note, sizeof(p.print_note), pd["origin"]);
    _cp_str(p.nozzle_id,  sizeof(p.nozzle_id),  pd["pump"]);
    _cp_str(p.fuel_type,  sizeof(p.fuel_type),  pd["fuel_type"]);
    static const char *const k_id_keys[] = { "NE_ID", "NID", "nid" };
    for (const char *k : k_id_keys) {
        if (!pd[k].isNull()) {
            _cp_str(p.event_id, sizeof(p.event_id), pd[k]);
            p.has_event_id = true;
            break;
        }
    }

    hsys_msg_t *jm = MsgPrintJob::create(id(), p);
    if (!jm) { _idle(k_poll_error_s); return; }
    LOG_MSG_INFO(CP_LOG, "printing message %s (job %lu, has_next=%d)",
                 _msg_id, (unsigned long)p.job_id, (int)_has_next);
    _step = STEP_PRINTING;
    send(jm, MODULE_PRINTER_ID);
}

void ModuleCloudPrint::_on_print_result(const hsys_msg_t &msg)
{
    if (_step != STEP_PRINTING) return;
    auto r = MsgPrintResult::deserialize(msg);
    if (r.job_id != _job_seq) return;

    if (r.result == PRINT_RESULT_OK) {
        _printed++;
        strncpy(_last_printed, _msg_id, sizeof(_last_printed) - 1);
        _send_ack(true);
    } else if (r.result == PRINT_RESULT_INVALID) {
        _rejected++;
        _send_ack(false);
    } else {
        // Printer not ready / write failed: leave the message queued and retry.
        _failed++;
        LOG_MSG_ERROR(CP_LOG, "print failed (result %u) — message %s kept in the queue, retry in %lus",
                      (unsigned)r.result, _msg_id, (unsigned long)k_print_retry_s);
        _idle(k_print_retry_s);
    }
}

void ModuleCloudPrint::_send_ack(bool ack)
{
    static char url[256];
    snprintf(url, sizeof(url), CP_URL_QUEUE "%s/messages/%s", _queue_id, _msg_id);
    snprintf(_auth, sizeof(_auth), "Basic %s", _basic_b64);
    _ack_ok = ack;
    _step = STEP_ACK;
    if (!_http(PAL_HTTP_METHOD_PATCH, url, _auth,
               ack ? "{\"status\":\"ACK\"}" : "{\"status\":\"REJECTED\"}", nullptr)) {
        _idle(k_poll_error_s);
    }
}

void ModuleCloudPrint::_handle_ack(int32_t status)
{
    bool ok = (status >= 200 && status < 300);
    LOG_MSG_INFO(CP_LOG, "%s %s → http %ld", _ack_ok ? "ACK" : "REJECTED", _msg_id, (long)status);
    if (ok && strcmp(_msg_id, _last_printed) == 0) _last_printed[0] = '\0';
    if (!ok) { _idle(k_poll_error_s); return; }   // next poll returns it again → re-ACK without reprint
    _idle();
    if (_has_next) _poll_pending = true;
}

// ── Status ──────────────────────────────────────────────────────────────────

void ModuleCloudPrint::_send_status(hsys_module_id_t to)
{
    if (to == HSYS_MODULE_ID_INVALID) return;
    MsgCloudPrintStatus::Payload s{};
    s.state             = _state;
    s.notify_subscribed = _notify_subscribed;
    s.last_http         = _last_http;
    s.polls             = _polls;
    s.printed           = _printed;
    s.failed            = _failed;
    s.rejected          = _rejected;
    strncpy(s.queue_id,     _queue_id,     sizeof(s.queue_id) - 1);
    strncpy(s.notify_topic, _notify_topic, sizeof(s.notify_topic) - 1);
    hsys_msg_t *m = MsgCloudPrintStatus::create(id(), s);
    if (m) send(m, to);
}
