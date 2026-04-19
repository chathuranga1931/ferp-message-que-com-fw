/**
 * @file sim_msg_inject.cpp
 * @brief Simulator-only message injection from the Python UI.
 *
 * Parses the outer SIM_MSG_INJECT JSON envelope, then dispatches to the
 * appropriate MsgXxx::from_json() static factory method.  Each message class
 * owns the parsing of its own payload fields — this file only routes.
 *
 * To add a new injectable message:
 *   1. Add from_json() to the message class (header + cpp, #ifdef FERP_SIMULATOR).
 *   2. Add a case here.
 *   Zero changes to mac_driver.cpp or CMakeLists needed.
 */

#include "sim_msg_inject.h"
#include "pal_logger.h"

// ArduinoJson (v7) — outer envelope parsing only
#include <ArduinoJson.h>

// HSYS bus
#include "hsys_msg.h"

// All injectable message classes
#include "app_msg_ids.h"
#include "msg_default_btn.h"
#include "msg_printer_btn.h"
#include "msg_sensor_data.h"
#include "msg_tick_1000ms.h"
#include "msg_spiffs_ready.h"
#include "msg_config_ready.h"
#include "msg_config_get.h"
#include "msg_config_set.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_stop_response.h"
#include "msg_timer_alarm.h"

#define __TAG__         "SIM_INJ "
#define SIM_INJ_LOG     true

#define MLOG(fmt, ...)  LOG_MSG_INFO( SIM_INJ_LOG, fmt, ##__VA_ARGS__)
#define MLOGE(fmt, ...) LOG_MSG_ERROR(SIM_INJ_LOG, fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Publish a NOTIFICATION message; logs if pool is exhausted. */
static void _publish(hsys_msg_t *msg, uint16_t msg_id)
{
    if (!msg) {
        MLOGE("from_json returned null for msg_id=0x%04X (pool full?)", (unsigned)msg_id);
        return;
    }
    hsys_msg_publish(msg);
}

/** Send a DIRECT message to a specific module. */
static void _send(hsys_msg_t *msg, uint16_t msg_id, hsys_module_id_t dst)
{
    if (!msg) {
        MLOGE("from_json returned null for msg_id=0x%04X (pool full?)", (unsigned)msg_id);
        return;
    }
    msg->receiver_id = dst;
    hsys_msg_send(msg, 0);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void sim_msg_inject_handle(const char *cmd_json)
{
    // ── Parse the outer envelope ─────────────────────────────────────────────
    // Expected shape:
    //   {"id":"SIM_MSG_INJECT","data":{"msg_id":N,"src_module_id":N,
    //    "dst_module_id":N,"payload":{...}}}
    JsonDocument outer;
    DeserializationError err = deserializeJson(outer, cmd_json);
    if (err) {
        MLOGE("JSON parse error: %s  buf=%s", err.c_str(), cmd_json);
        return;
    }

    JsonObject data = outer["data"].as<JsonObject>();
    if (data.isNull()) {
        MLOGE("missing 'data' field in: %s", cmd_json);
        return;
    }

    uint16_t         msg_id  = data["msg_id"].as<uint16_t>();
    hsys_module_id_t src     = data["src_module_id"].as<uint16_t>();
    hsys_module_id_t dst     = data["dst_module_id"].as<uint16_t>();

    // Serialise the inner payload object back to a compact JSON string so
    // each from_json() receives a self-contained, flat JSON object.
    char payload_buf[384] = "{}";
    if (!data["payload"].isNull()) {
        serializeJson(data["payload"], payload_buf, sizeof(payload_buf));
    }

    MLOG("inject msg_id=0x%04X src=%u dst=%u payload=%s",
         (unsigned)msg_id, (unsigned)src, (unsigned)dst, payload_buf);

    // ── Dispatch ─────────────────────────────────────────────────────────────
    switch (msg_id)
    {
        // ── Buttons ──────────────────────────────────────────────────────────
        case MSG_ID_DEFAULT_BTN:
            _publish(MsgDefaultBtn::from_json(payload_buf, src), msg_id);
            break;

        case MSG_ID_PRINTER_BTN:
            _publish(MsgPrinterBtn::from_json(payload_buf, src), msg_id);
            break;

        // ── Sensor ───────────────────────────────────────────────────────────
        case MSG_ID_SENSOR_DATA:
            _publish(MsgSensorData::from_json(payload_buf, src), msg_id);
            break;

        // ── System ───────────────────────────────────────────────────────────
        case MSG_ID_TICK_1000MS:
            _publish(MsgTick1000ms::from_json(payload_buf, src), msg_id);
            break;

        case MSG_ID_SPIFFS_READY:
            _publish(MsgSpiffsReady::from_json(payload_buf, src), msg_id);
            break;

        // ── Config ───────────────────────────────────────────────────────────
        case MSG_ID_CONFIG_READY:
            _publish(MsgConfigReady::from_json(payload_buf, src), msg_id);
            break;

        case MSG_ID_CONFIG_GET:
            _publish(MsgConfigGet::from_json(payload_buf, src), msg_id);
            break;

        case MSG_ID_CONFIG_SET:
            _publish(MsgConfigSet::from_json(payload_buf, src), msg_id);
            break;

        // ── Timer ────────────────────────────────────────────────────────────
        case MSG_ID_TIMER_START:
            _publish(MsgTimerStart::from_json(payload_buf, src), msg_id);
            break;

        case MSG_ID_TIMER_STOP:
            _publish(MsgTimerStop::from_json(payload_buf, src), msg_id);
            break;

        // Timer responses and alarm are DIRECT — deliver to dst_module_id
        case MSG_ID_TIMER_START_RESPONSE:
            _send(MsgTimerStartResponse::from_json(payload_buf, src), msg_id, dst);
            break;

        case MSG_ID_TIMER_STOP_RESPONSE:
            _send(MsgTimerStopResponse::from_json(payload_buf, src), msg_id, dst);
            break;

        case MSG_ID_TIMER_ALARM:
            _send(MsgTimerAlarm::from_json(payload_buf, src), msg_id, dst);
            break;

        default:
            MLOGE("unknown msg_id=0x%04X — add a case in sim_msg_inject.cpp", (unsigned)msg_id);
            break;
    }
}
