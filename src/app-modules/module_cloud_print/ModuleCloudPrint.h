// ModuleCloudPrint.h
//
// ModuleCloudPrint — prints receipts queued in the cloud (fuel-iot-core).
//
// Port of the legacy printer's ferp_client.cpp / printer_server.cpp cloud
// path onto the HSYS bus.  All HTTPS goes through ModuleHttp; the MQTT
// notification topic goes through ModuleMqtt (MsgMqttSubscribe).
//
//   1. Registration (same SAS-AC1 handshake as ModuleCubeSphere):
//        GET  /bootstrap/core/v1/device              → 401 + nonce
//        GET  /bootstrap/core/v1/device  (SAS-AC1)   → device_id + secret
//        GET  /ingress/core/v1/device/config (Basic) → printer-queue-id,
//                                                      printer-notify-topic
//   2. MsgMqttSubscribe(printer-notify-topic) → ModuleMqtt.  Every message on
//      that topic arrives here as MsgMqttExtData and triggers a queue poll.
//      A slow timer poll (cp_poll_s) is the fallback when MQTT is down.
//   3. Poll:  GET /ingress/ext.queue/v1/queue/<id>/messages?timezone=…
//      → one message (+ has_next) → MsgPrintJob → ModulePrinter
//   4. MsgPrintResult OK      → PATCH …/messages/<id> {"status":"ACK"}
//      content not printable  → PATCH {"status":"REJECTED"}
//      printer failure        → no ACK; the job stays queued and is retried
//      (legacy bug fix: the old firmware ACKed even when printing failed).
//
// Disabled entirely while en_cloud_print = false.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "printer_types.h"
#include "pal_http_client.h"
#include <stdint.h>

#define MODULE_CLOUD_PRINT_NAME  "cloudprn"

class ModuleCloudPrint : public HsysModule
{
public:
    ModuleCloudPrint() : HsysModule(MODULE_CLOUD_PRINT_ID, MODULE_CLOUD_PRINT_NAME) {}
    static ModuleCloudPrint *instance();

    /** Root CA for the cloud endpoints (static lifetime). */
    void set_root_ca(const char *pem) { _root_ca = pem; }

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    enum Step : uint8_t {
        STEP_IDLE,
        STEP_REG_1,          ///< bootstrap without auth (expect 401 + nonce)
        STEP_REG_WAIT_2,     ///< 1 s pause between step 1 and 2 (lets the TLS session close)
        STEP_REG_2,          ///< bootstrap with SAS-AC1
        STEP_REG_3,          ///< device config
        STEP_POLL,           ///< GET queue message
        STEP_PRINTING,       ///< waiting for MsgPrintResult
        STEP_ACK,            ///< PATCH status
    };

    const char *_root_ca   = nullptr;
    bool        _enabled   = false;
    bool        _internet  = false;
    uint8_t     _state     = CLOUD_PRINT_DISABLED;   ///< cloud_print_state_t
    Step        _step      = STEP_IDLE;
    bool        _http_busy = false;

    // Timing (1 s ticks)
    uint32_t _wait_s        = 0;   ///< countdown before the next action (0 = none)
    uint32_t _poll_age_s    = 0;   ///< seconds since the last poll
    uint32_t _http_age_s    = 0;   ///< seconds the current HTTP request is in flight
    bool     _poll_pending  = false;

    // Registration results
    char _nonce[128]       = {};   ///< the cloud currently sends 68 characters
    char _auth[300]        = {};   ///< "SAS-AC1 …" or "Basic …"
    char _basic_b64[200]   = {};
    char _device_id[48]    = {};
    char _queue_id[48]     = {};
    char _notify_topic[128]= {};
    bool _notify_subscribed = false;

    // Current queue message
    char     _msg_id[64]        = {};
    char     _last_printed[64]  = {};   ///< message id printed but maybe not yet ACKed
    bool     _has_next          = false;
    bool     _ack_ok            = true;  ///< ACK (true) or REJECTED (false)
    uint32_t _job_seq           = 0;

    // Counters for MsgCloudPrintStatus
    int32_t  _last_http = 0;
    uint32_t _polls     = 0;
    uint32_t _printed   = 0;
    uint32_t _failed    = 0;
    uint32_t _rejected  = 0;

    void _on_config_ready();
    void _on_internet(bool up);
    void _on_tick();
    void _on_http_header(const hsys_msg_t &msg);
    void _on_http_result(const hsys_msg_t &msg);
    void _on_print_result(const hsys_msg_t &msg);
    void _on_notify();
    void _send_status(hsys_module_id_t to);

    void _set_state(uint8_t s);
    void _start_registration();
    void _reg_step_1();
    void _reg_step_2();
    void _reg_step_3();
    void _reg_failed(const char *why);
    void _poll();
    void _send_ack(bool ack);
    void _idle(uint32_t wait_s = 0);

    void _handle_reg_1(int32_t status);
    void _handle_reg_2(int32_t status, const char *body, uint32_t len);
    void _handle_reg_3(int32_t status, const char *body, uint32_t len);
    void _handle_poll(int32_t status, const char *body, uint32_t len);
    void _handle_ack(int32_t status);

    bool _http(pal_http_method_t method, const char *url, const char *auth,
               const char *body, const char *collect_key);
};
