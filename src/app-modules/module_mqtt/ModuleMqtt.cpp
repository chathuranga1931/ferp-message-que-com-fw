// ModuleMqtt.cpp
//
// ModuleMqtt implementation — see ModuleMqtt.h for architecture notes.

#include "ModuleMqtt.h"

#include "pal_logger.h"
#include "app_msg_codec.h"
#include "hsys_msg.h"
#include "app_config.h"
#include "app_device_info.h"

// Messages this module subscribes to
#include "msg_config_ready.h"
#include "msg_config_mqtt.h"
#include "msg_config_wifi.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_value.h"
#include "msg_internet_status.h"
#include "msg_fuel_pumped.h"
#include "msg_nozzle_state.h"
#include "msg_ota_event.h"
#include "msg_ota_progress.h"
#include "msg_config_cloud.h"
#include "msg_config_ota.h"
#include "msg_mqtt_status.h"
#include "msg_dev_info_value.h"
#include "msg_pool_json.h"          // MsgPoolJson (0x0208) — pool snapshot broadcast
#include "msg_file_list_spiffs.h"    // MsgFileListSpiffs (0x020A) — SPIFFS listing broadcast

// OTA source messages
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_abort_request.h"

#include "crc32.h"
#include "mqtt_auth.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define __TAG__ "MOD_MQT "
#define MQTT_LOG true

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleMqtt s_instance;
ModuleMqtt *ModuleMqtt::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Strip hyphens and lower-case a UUID for use as a topic segment. */
static void uuid_to_topic_id(const char *uuid, char *out, size_t out_len)
{
    size_t j = 0;
    for (size_t i = 0; uuid[i] != '\0' && j < out_len - 1; i++) {
        if (uuid[i] != '-') out[j++] = (char)tolower((unsigned char)uuid[i]);
    }
    out[j] = '\0';
}

// ---------------------------------------------------------------------------
// set_ota_targets / _resolve_ota_target
// ---------------------------------------------------------------------------

void ModuleMqtt::set_ota_targets(const mqtt_ota_target_t *table, uint8_t count)
{
    _ota_name_table       = table;
    _ota_name_table_count = count;
}

/** Resolve a wire-protocol OTA target name to target_idx.
 *  Accepts a decimal numeric string ("0", "1", ...) or any name present in
 *  the table supplied via set_ota_targets().  Returns 0xFF on failure. */
uint8_t ModuleMqtt::_resolve_ota_target(const char *name) const
{
    if (!name || name[0] == '\0') return 0xFF;
    // Accept decimal integer string directly
    char *end;
    long v = strtol(name, &end, 10);
    if (*end == '\0' && end != name && v >= 0 && v < 255) return (uint8_t)v;
    // Walk the app-supplied name table
    for (uint8_t i = 0; i < _ota_name_table_count; i++) {
        if (_ota_name_table[i].name && strcmp(name, _ota_name_table[i].name) == 0)
            return _ota_name_table[i].target_idx;
    }
    return 0xFF;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ModuleMqtt::init()
{
    // Config lifecycle
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigMqtt::ID);    // DIRECT response from ModuleConfig

    // Connectivity
    subscribe(MsgInternetStatus::ID);

    // Outbound encode-and-publish messages (response → resp, event → evt)
    subscribe(MsgConfigWifi::ID);    // DIRECT response from ModuleConfig
    subscribe(MsgConfigCloud::ID);   // DIRECT response from ModuleConfig
    subscribe(MsgConfigOta::ID);     // DIRECT response from ModuleConfig
    subscribe(MsgConfigValue::ID);   // DIRECT response from ModuleConfig (MsgConfigGetKey)
    subscribe(MsgFuelPumped::ID);    // NOTIFICATION → evt
    subscribe(MsgNozzleState::ID);   // NOTIFICATION → evt
    subscribe(MsgOtaEvent::ID);      // NOTIFICATION → evt
    subscribe(MsgOtaProgress::ID);   // NOTIFICATION → evt

    // OTA source — responses from OtaModule
    subscribe(MsgOtaStartResponse::ID);  // DIRECT from OtaModule
    subscribe(MsgOtaDriverResponse::ID); // DIRECT from OtaModule

    // Device identity — notified when a field is updated by permitted writer
    subscribe(MsgDevInfoValue::ID);
    subscribe(MsgPoolJson::ID);      // NOTIFICATION → pool snapshot response
    subscribe(MsgFileListSpiffs::ID); // NOTIFICATION → SPIFFS file listing response

    LOG_MSG_INFO(MQTT_LOG, "init — state=WAIT_CONFIG");
}

// ---------------------------------------------------------------------------
// on_msg_received
// ---------------------------------------------------------------------------

void ModuleMqtt::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MsgConfigReady::ID:
            // Request our broker config from ModuleConfig
            {
                MsgConfigGetMqtt::Payload req{};
                req.source_module_id = id();
                hsys_msg_t *m = MsgConfigGetMqtt::create(id(), req);
                if (m) publish(m);
            }
            break;

        case MsgConfigMqtt::ID:
            _on_config_mqtt(msg);
            break;

        case MsgInternetStatus::ID:
            _on_internet_status(msg);
            // Also forward as outbound event after we are connected
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, true /*evt*/);
            }
            break;

        // ── Outbound DIRECT responses ──────────────────────────────────────
        // These arrive as DIRECT messages sent back to this module
        case MsgConfigWifi::ID:
            if (_state == STATE_CONNECTED && msg.receiver_id == id()) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        case MsgConfigCloud::ID:
            if (_state == STATE_CONNECTED && msg.receiver_id == id()) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        case MsgConfigOta::ID:
            if (_state == STATE_CONNECTED && msg.receiver_id == id()) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        case MsgConfigValue::ID:
            if (_state == STATE_CONNECTED && msg.receiver_id == id()) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        // ── Outbound NOTIFICATION events ───────────────────────────────────
        case MsgFuelPumped::ID:
        case MsgNozzleState::ID:
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, true /*evt*/);
            }
            break;

        case MsgOtaProgress::ID:
            // Throttle: only forward to MQTT when the percentage value changes.
            // The message still flows on the internal bus every chunk to keep
            // OtaModule's inactivity watchdog alive.
            if (_state == STATE_CONNECTED) {
                auto pp = MsgOtaProgress::deserialize(msg);
                if (pp.percent != _last_progress_pct) {
                    _last_progress_pct = pp.percent;
                    _on_outbound_msg(msg, true /*evt*/);
                }
            }
            break;

        case MsgOtaEvent::ID:
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, true /*evt*/);
            }
            // If we are waiting for OtaModule to confirm ota_complete, respond now
            if (_ota_state == MQTT_OTA_COMPLETING) {
                auto ep = MsgOtaEvent::deserialize(msg);
                char resp[MODULE_MQTT_OTA_RESP_MAX];
                if (ep.event == OTA_EVENT_COMPLETE) {
                    snprintf(resp, sizeof(resp),
                             "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"ok\"}",
                             (unsigned)_ota_seq);
                    _publish_ota_resp(resp);
                    LOG_MSG_INFO(MQTT_LOG, "OTA: complete confirmed by OtaModule");
                } else if (ep.event == OTA_EVENT_SESSION_ABORTED ||
                           ep.event == OTA_EVENT_TIMEOUT) {
                    snprintf(resp, sizeof(resp),
                             "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"error\","
                             "\"code\":\"aborted\"}",
                             (unsigned)_ota_seq);
                    _publish_ota_resp(resp);
                    LOG_MSG_WARNING(MQTT_LOG, "OTA: complete aborted by OtaModule (event=%d)",
                                    (int)ep.event);
                }
                _ota_reset();
            }
            break;

        // ── OTA source: responses from OtaModule ──────────────────────────
        case MsgOtaStartResponse::ID: {
            if (_ota_state != MQTT_OTA_REQUESTING_SESSION) break;
            auto p = MsgOtaStartResponse::deserialize(msg);
            if (p.result == OTA_START_ACCEPTED) {
                hsys_msg_t *req = MsgOtaRequestDriver::create(id());
                if (req) {
                    send(req, MODULE_OTA_ID);
                    _ota_state = MQTT_OTA_REQUESTING_DRIVER;
                    LOG_MSG_INFO(MQTT_LOG, "OTA: session accepted → requesting driver");
                } else {
                    LOG_MSG_ERROR(MQTT_LOG, "OTA: pool full — can't send MsgOtaRequestDriver");
                    char resp[MODULE_MQTT_OTA_RESP_MAX];
                    snprintf(resp, sizeof(resp),
                             "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                             "\"code\":\"internal\"}",
                             (unsigned)_ota_seq);
                    _publish_ota_resp(resp);
                    _ota_reset();
                }
            } else {
                const char *code = (p.result == OTA_START_REJECTED_BUSY) ? "busy" :
                                   (p.result == OTA_START_REJECTED_UNKNOWN_TARGET) ? "unknown_target" :
                                   "rejected";
                char resp[MODULE_MQTT_OTA_RESP_MAX];
                snprintf(resp, sizeof(resp),
                         "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                         "\"code\":\"%s\"}",
                         (unsigned)_ota_seq, code);
                _publish_ota_resp(resp);
                LOG_MSG_INFO(MQTT_LOG, "OTA: session rejected result=%d", (int)p.result);
                _ota_reset();
            }
            break;
        }

        case MsgOtaDriverResponse::ID: {
            if (_ota_state != MQTT_OTA_REQUESTING_DRIVER) break;
            auto p = MsgOtaDriverResponse::deserialize(msg);
            _ota_driver = p.driver;
            _ota_ctx    = p.ctx;

            // Open write session on the driver
            ota_fs_err_t err = _ota_driver->fopen(_ota_ctx, nullptr, OTA_FS_OPEN_WRITE);
            if (err != OTA_FS_OK) {
                LOG_MSG_ERROR(MQTT_LOG, "OTA: fopen failed err=%d", (int)err);
                MsgOtaAbortRequest::Payload ap{ OTA_ABORT_WRITE_ERROR, {} };
                hsys_msg_t *am = MsgOtaAbortRequest::create(id(), ap);
                if (am) send(am, MODULE_OTA_ID);
                char resp[MODULE_MQTT_OTA_RESP_MAX];
                snprintf(resp, sizeof(resp),
                         "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                         "\"code\":\"driver_fopen\"}",
                         (unsigned)_ota_seq);
                _publish_ota_resp(resp);
                _ota_reset();
                break;
            }
            _ota_fopen_done     = true;
            _ota_state          = MQTT_OTA_ACTIVE;
            _ota_expected_offset = 0;
            _ota_bytes_written  = 0;
            _ota_running_crc    = 0xFFFFFFFF;

            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"ok\"}",
                     (unsigned)_ota_seq);
            _publish_ota_resp(resp);
            LOG_MSG_INFO(MQTT_LOG, "OTA: driver ready target=%u size=%lu → ACTIVE",
                         (unsigned)_ota_target_idx, (unsigned long)_ota_total_size);
            break;
        }

        case MsgDevInfoValue::ID:
            if (msg.receiver_id == (hsys_module_id_t)0) {
                // Broadcast notification from ModuleDeviceInfo after a successful write
                _on_dev_info_modified(msg);
            } else {
                // Direct read response — forward to the resp topic so the tool receives it
                _on_outbound_msg(msg, false);
            }
            break;

        case MsgPoolJson::ID:
            // Pool status snapshot — always a NOTIFICATION broadcast
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        case MsgFileListSpiffs::ID:
            // SPIFFS file listing — always a NOTIFICATION broadcast
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, false /*resp*/);
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// _on_config_mqtt
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_config_mqtt(const hsys_msg_t &msg)
{
    // If already configured, this is a response to an inbound cmd — forward it
    if (_state != STATE_WAIT_CONFIG) {
        if (_state == STATE_CONNECTED && msg.receiver_id == id()) {
            _on_outbound_msg(msg, false /*resp*/);
        }
        return;
    }

    auto p = MsgConfigMqtt::deserialize(msg);
    if (p.host[0] == '\0') {
        LOG_MSG_WARNING(MQTT_LOG, "MsgConfigMqtt: empty host, staying in WAIT_CONFIG");
        return;
    }

    // Build PAL config from the received Payload
    pal_mqtt_config_t cfg{};
    pal_mqtt_get_default_config(&cfg);

    snprintf(cfg.broker_uri, sizeof(cfg.broker_uri), "mqtt://%s", p.host);
    cfg.port = (uint16_t)p.port;
    strncpy(cfg.username, p.user,     sizeof(cfg.username) - 1);
    strncpy(cfg.password, p.password, sizeof(cfg.password) - 1);
    // Use device_uuid as client ID (set from app_config by the device)
    // We'll leave client_id empty to let pal_mac_mqtt auto-generate a unique one
    cfg.keepalive            = 60;
    cfg.reconnect_timeout_ms = 5000;

    // Build topic paths using hardware MAC address + hardcoded device group.
    // hw_address is populated by ModuleDeviceInfo::init() from eFuse before any
    // module runs, so it is always valid here.
    const app_device_info_t *dev_info = app_device_info_get();
    _build_topics("ferp-com", dev_info->device_group, dev_info->hw_address);

    // Use hw_address as MQTT client ID (stable across reboots, unique per device)
    strncpy(cfg.client_id, dev_info->hw_address, sizeof(cfg.client_id) - 1);

    if (_client) {
        pal_mqtt_client_destroy(_client);
        _client = nullptr;
    }
    _client = pal_mqtt_client_init(&cfg, &ModuleMqtt::s_pal_event_cb, this);
    if (!_client) {
        LOG_MSG_WARNING(MQTT_LOG, "pal_mqtt_client_init failed");
        return;
    }

    _state = STATE_WAIT_INTERNET;
    LOG_MSG_INFO(MQTT_LOG, "config received — broker=%s:%u  user='%s'  client_id='%s'  state=WAIT_INTERNET",
                 p.host, p.port, p.user, cfg.client_id);
}

// ---------------------------------------------------------------------------
// _on_internet_status
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_internet_status(const hsys_msg_t &msg)
{
    auto p = MsgInternetStatus::deserialize(msg);

    if (p.connected) {
        if (_state == STATE_WAIT_INTERNET) {
            if (!_client) {
                LOG_MSG_WARNING(MQTT_LOG, "internet up but no client — need config");
                return;
            }
            _state = STATE_CONNECTING;
            LOG_MSG_INFO(MQTT_LOG, "internet up — starting MQTT client");
            pal_mqtt_client_start(_client);
        }
    } else {
        if (_state == STATE_CONNECTED || _state == STATE_CONNECTING) {
            LOG_MSG_INFO(MQTT_LOG, "internet lost — stopping MQTT client");
            if (_client) pal_mqtt_client_stop(_client);
            _state = STATE_WAIT_INTERNET;
        }
    }
}

// ---------------------------------------------------------------------------
// _on_pal_connected
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_pal_connected()
{
    _state = STATE_CONNECTED;
    LOG_MSG_INFO(MQTT_LOG, "MQTT connected — subscribing to cmd topics");

    // Notify other modules (e.g. sim bridge → UI LED)
    {
        MsgMqttStatus::Payload sp{}; sp.connected = true;
        hsys_msg_t *m = MsgMqttStatus::create(id(), sp);
        if (m) publish(m);
    }

    // Subscribe only to the exact device cmd topic.
    // A wildcard group subscription (e.g. ferp/ferp-com/{group}/+/cmd) must NOT
    // be used here: the broker delivers a message to every matching subscription,
    // so subscribing to both the exact topic AND a wildcard that covers it causes
    // every unicast cmd to be delivered twice.
    // Group / broadcast commands should be published to a dedicated group topic
    // (e.g. ferp/ferp-com/{group}/broadcast/cmd) that does NOT overlap with the
    // device-specific cmd topic.
    pal_mqtt_client_subscribe(_client, _cmd_topic, PAL_MQTT_QOS_1);
    pal_mqtt_client_subscribe(_client, _ota_ctrl_topic, PAL_MQTT_QOS_1);
    pal_mqtt_client_subscribe(_client, _ota_data_topic, PAL_MQTT_QOS_1);

    LOG_MSG_INFO(MQTT_LOG, "mac cmd topic:      %s", _cmd_topic);
    LOG_MSG_INFO(MQTT_LOG, "mac ota/ctrl topic: %s", _ota_ctrl_topic);
    LOG_MSG_INFO(MQTT_LOG, "mac ota/data topic: %s", _ota_data_topic);

    if (_uuid_active) {
        pal_mqtt_client_subscribe(_client, _uuid_cmd_topic,      PAL_MQTT_QOS_1);
        pal_mqtt_client_subscribe(_client, _uuid_ota_ctrl_topic, PAL_MQTT_QOS_1);
        pal_mqtt_client_subscribe(_client, _uuid_ota_data_topic, PAL_MQTT_QOS_1);
        LOG_MSG_INFO(MQTT_LOG, "uuid cmd topic:      %s", _uuid_cmd_topic);
        LOG_MSG_INFO(MQTT_LOG, "uuid ota/ctrl topic: %s", _uuid_ota_ctrl_topic);
        LOG_MSG_INFO(MQTT_LOG, "uuid ota/data topic: %s", _uuid_ota_data_topic);
    }

    // Default response topics to MAC-based until a cmd overrides them
    _active_resp_topic     = _resp_topic;
    _active_ota_resp_topic = _ota_resp_topic;
}

// ---------------------------------------------------------------------------
// _on_pal_disconnected
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_pal_disconnected()
{
    if (_state == STATE_CONNECTED) {
        _state = STATE_CONNECTING;
        LOG_MSG_INFO(MQTT_LOG, "MQTT disconnected — waiting for reconnect");

        // Notify other modules (e.g. sim bridge → UI LED)
        MsgMqttStatus::Payload sp{}; sp.connected = false;
        hsys_msg_t *m = MsgMqttStatus::create(id(), sp);
        if (m) publish(m);
    }

    // Abort any in-progress OTA session
    if (_ota_state != MQTT_OTA_IDLE) {
        LOG_MSG_WARNING(MQTT_LOG, "OTA: MQTT disconnected — aborting OTA session");
        if (_ota_state == MQTT_OTA_ACTIVE || _ota_state == MQTT_OTA_COMPLETING) {
            if (_ota_fopen_done && _ota_driver) {
                _ota_driver->ferase(_ota_ctx);
            }
            MsgOtaAbortRequest::Payload ap{ OTA_ABORT_SOURCE_DISCONNECTED, {} };
            hsys_msg_t *am = MsgOtaAbortRequest::create(id(), ap);
            if (am) send(am, MODULE_OTA_ID);
        }
        _ota_reset();
    }
}

// ---------------------------------------------------------------------------
// _on_pal_data  — inbound cmd from broker
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_pal_data(const pal_mqtt_message_t *m)
{
    if (!m || !m->data || m->data_len == 0) return;

    // Route OTA topics before JSON envelope parsing
    if (m->topic_len == strlen(_ota_ctrl_topic) &&
        strncmp(m->topic, _ota_ctrl_topic, m->topic_len) == 0) {
        _active_ota_resp_topic = _ota_resp_topic;
        _handle_ota_ctrl(m);
        return;
    }
    if (m->topic_len == strlen(_ota_data_topic) &&
        strncmp(m->topic, _ota_data_topic, m->topic_len) == 0) {
        _handle_ota_data(m);
        return;
    }
    if (_uuid_active) {
        if (m->topic_len == strlen(_uuid_ota_ctrl_topic) &&
            strncmp(m->topic, _uuid_ota_ctrl_topic, m->topic_len) == 0) {
            _active_ota_resp_topic = _uuid_ota_resp_topic;
            _handle_ota_ctrl(m);
            return;
        }
        if (m->topic_len == strlen(_uuid_ota_data_topic) &&
            strncmp(m->topic, _uuid_ota_data_topic, m->topic_len) == 0) {
            _handle_ota_data(m);
            return;
        }
    }

    // Parse envelope: { "seq": N, "msg": "...", "data": {...} }
    StaticJsonDocument<MODULE_MQTT_ENV_MAX> doc;
    DeserializationError err = deserializeJson(doc, m->data, m->data_len);
    if (err) {
        LOG_MSG_WARNING(MQTT_LOG, "envelope parse error: %s on topic: %.*s",
                        err.c_str(), (int)m->topic_len, m->topic);
        return;
    }

    uint32_t    seq      = doc["seq"] | (uint32_t)0;
    const char *msg_name = doc["msg"] | "";
    if (msg_name[0] == '\0') {
        LOG_MSG_WARNING(MQTT_LOG, "envelope missing 'msg' field");
        return;
    }

    // ── Authenticate ───────────────────────────────────────────────────────
    // Every inbound cmd must carry hop_idx (key selector) and hash
    // (mqtt_auth_hash of seq using the selected shared key).  Messages that
    // fail this check are silently discarded — no response is sent so an
    // attacker gets no oracle to probe against.
    {
        int         hop_raw  = doc["hop_idx"] | (int)-1;
        const char *hash_hex = doc["hash"]    | "";
        if (hop_raw < 0 || hop_raw >= (int)MQTT_AUTH_KEY_COUNT ||
            !mqtt_auth_verify((uint8_t)hop_raw, hash_hex, seq)) {
            LOG_MSG_WARNING(MQTT_LOG,
                            "cmd auth FAILED seq=%u hop=%d msg='%s' — discarded",
                            (unsigned)seq, hop_raw, msg_name);
            return;
        }
    }

    // Serialize the "data" sub-object back to JSON string for the codec
    char data_json[MODULE_MQTT_DATA_MAX];
    if (doc.containsKey("data")) {
        serializeJson(doc["data"], data_json, sizeof(data_json));
    } else {
        strncpy(data_json, "{}", sizeof(data_json));
    }

    _last_cmd_seq = seq;

    // Determine which resp topic to use for the response based on which
    // cmd topic this message arrived on.
    if (_uuid_active &&
        m->topic_len == strlen(_uuid_cmd_topic) &&
        strncmp(m->topic, _uuid_cmd_topic, m->topic_len) == 0) {
        _active_resp_topic = _uuid_resp_topic;
    } else {
        _active_resp_topic = _resp_topic;
    }

    LOG_MSG_INFO(MQTT_LOG, "cmd seq=%u msg='%s'", seq, msg_name);

    // Decode into an HSYS message
    hsys_msg_t *decoded = app_msg_codec_decode(msg_name, data_json, id());
    if (!decoded) {
        LOG_MSG_WARNING(MQTT_LOG, "unknown or undecodeable msg: %s", msg_name);
        return;
    }

    // If the cmd arrived on a wildcard/group topic (not the exact device cmd
    // topic), only process it when multicast_resp is enabled for this message
    // type.  With the current single-subscription model this is never triggered,
    // but it is kept as a safety guard for future group-topic support.
    bool exact_topic = (m->topic_len == strlen(_cmd_topic) &&
                        strncmp(m->topic, _cmd_topic, m->topic_len) == 0);
    if (!exact_topic && !app_msg_mqtt_route_is_multicast(decoded->msg_id)) {
        LOG_MSG_INFO(MQTT_LOG, "cmd '%s' via wildcard topic — multicast_resp=false, ignored", msg_name);
        hsys_msg_release(decoded);
        return;
    }

    // Route: DIRECT to specific module or NOTIFICATION broadcast
    hsys_module_id_t dest = app_msg_mqtt_route_get_dest(decoded->msg_id);
    if (dest != (hsys_module_id_t)0) {
        send(decoded, dest);
    } else {
        publish(decoded);
    }
}

// ---------------------------------------------------------------------------
// _on_outbound_msg  — encode an HSYS message and publish to MQTT
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_outbound_msg(const hsys_msg_t &msg, bool is_evt)
{
    if (!_client) return;

    char msg_name[MODULE_MQTT_MSG_NAME_MAX] = {};
    char data_json[MODULE_MQTT_DATA_MAX]    = {};

    int32_t ret = app_msg_codec_encode(&msg,
                                        msg_name, sizeof(msg_name),
                                        data_json, sizeof(data_json));
    if (ret < 0) return; // not encodeable

    // For responses, echo back the last received cmd seq; for events, use 0
    uint32_t seq = is_evt ? 0U : _last_cmd_seq;

    const char *topic = is_evt ? _evt_topic
                               : (_active_resp_topic ? _active_resp_topic : _resp_topic);
    _publish_envelope(topic, seq, msg_name, data_json);
}

// ---------------------------------------------------------------------------
// _build_topics
// ---------------------------------------------------------------------------

void ModuleMqtt::_build_topics(const char *dev_type, const char *group,
                                 const char *device_id)
{
    snprintf(_cmd_topic,      sizeof(_cmd_topic),
             "ferp/%s/%s/%s/cmd",      dev_type, group, device_id);
    snprintf(_resp_topic,     sizeof(_resp_topic),
             "ferp/%s/%s/%s/resp",     dev_type, group, device_id);
    snprintf(_evt_topic,      sizeof(_evt_topic),
             "ferp/%s/%s/%s/evt",      dev_type, group, device_id);
    snprintf(_ota_ctrl_topic, sizeof(_ota_ctrl_topic),
             "ferp/%s/%s/%s/ota/ctrl", dev_type, group, device_id);
    snprintf(_ota_data_topic, sizeof(_ota_data_topic),
             "ferp/%s/%s/%s/ota/data", dev_type, group, device_id);
    snprintf(_ota_resp_topic, sizeof(_ota_resp_topic),
             "ferp/%s/%s/%s/ota/resp", dev_type, group, device_id);
}

// ---------------------------------------------------------------------------
// _build_uuid_topics
// ---------------------------------------------------------------------------

void ModuleMqtt::_build_uuid_topics(const char *group, const char *uuid_topic_id)
{
    snprintf(_uuid_cmd_topic,      sizeof(_uuid_cmd_topic),
             "ferp/ferp-com/%s/%s/cmd",      group, uuid_topic_id);
    snprintf(_uuid_resp_topic,     sizeof(_uuid_resp_topic),
             "ferp/ferp-com/%s/%s/resp",     group, uuid_topic_id);
    snprintf(_uuid_ota_ctrl_topic, sizeof(_uuid_ota_ctrl_topic),
             "ferp/ferp-com/%s/%s/ota/ctrl", group, uuid_topic_id);
    snprintf(_uuid_ota_data_topic, sizeof(_uuid_ota_data_topic),
             "ferp/ferp-com/%s/%s/ota/data", group, uuid_topic_id);
    snprintf(_uuid_ota_resp_topic, sizeof(_uuid_ota_resp_topic),
             "ferp/ferp-com/%s/%s/ota/resp", group, uuid_topic_id);
}

// ---------------------------------------------------------------------------
// _on_dev_info_modified  — react when cloud module provisions device UUID
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_dev_info_modified(const hsys_msg_t &msg)
{
    auto p = MsgDevInfoValue::deserialize(msg);
    if (p.key != DEV_INFO_KEY_DEVICE_UUID) return;

    // Read the freshly-written UUID directly from the live device info struct
    const app_device_info_t *dev_info = app_device_info_get();
    if (dev_info->device_uuid[0] == '\0') return;

    char uuid_id[40];
    uuid_to_topic_id(dev_info->device_uuid, uuid_id, sizeof(uuid_id));
    _build_uuid_topics(dev_info->device_group, uuid_id);
    _uuid_active = true;

    LOG_MSG_INFO(MQTT_LOG, "UUID provisioned — uuid cmd topic: %s", _uuid_cmd_topic);

    if (_state == STATE_CONNECTED && _client) {
        pal_mqtt_client_subscribe(_client, _uuid_cmd_topic,      PAL_MQTT_QOS_1);
        pal_mqtt_client_subscribe(_client, _uuid_ota_ctrl_topic, PAL_MQTT_QOS_1);
        pal_mqtt_client_subscribe(_client, _uuid_ota_data_topic, PAL_MQTT_QOS_1);
        LOG_MSG_INFO(MQTT_LOG, "subscribed to UUID topics");
    }
}

// ---------------------------------------------------------------------------
// _publish_envelope
// ---------------------------------------------------------------------------

void ModuleMqtt::_publish_envelope(const char *topic, uint32_t seq,
                                    const char *msg_name, const char *data_json)
{
    if (!_client || _state != STATE_CONNECTED) return;

    char env[MODULE_MQTT_ENV_MAX];
    int n = snprintf(env, sizeof(env),
                     "{\"seq\":%u,\"msg\":\"%s\",\"data\":%s}",
                     (unsigned)seq, msg_name, data_json);
    if (n <= 0 || (size_t)n >= sizeof(env)) return;

    pal_mqtt_client_publish(_client, topic, env, (size_t)n, PAL_MQTT_QOS_0, false);
    LOG_MSG_INFO(MQTT_LOG, "pub [%s] %s", topic, env);
}

// ---------------------------------------------------------------------------
// PAL event callback (static trampoline → instance method)
// ---------------------------------------------------------------------------

void ModuleMqtt::s_pal_event_cb(pal_mqtt_event_data_t *ev)
{
    if (!ev || !ev->user_data) return;
    auto *self = static_cast<ModuleMqtt *>(ev->user_data);

    switch (ev->event_type) {
        case PAL_MQTT_EVENT_CONNECTED:
            self->_on_pal_connected();
            break;
        case PAL_MQTT_EVENT_DISCONNECTED:
            self->_on_pal_disconnected();
            break;
        case PAL_MQTT_EVENT_DATA:
            self->_on_pal_data(&ev->data.message);
            break;
        case PAL_MQTT_EVENT_ERROR:
            LOG_MSG_WARNING(MQTT_LOG, "PAL MQTT error code=%d", (int)ev->data.error_code);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// _ota_reset  — return OTA source state to IDLE
// ---------------------------------------------------------------------------

void ModuleMqtt::_ota_reset()
{
    _ota_state           = MQTT_OTA_IDLE;
    _ota_driver          = nullptr;
    _ota_ctx             = nullptr;
    _ota_target_idx      = 0;
    _ota_total_size      = 0;
    _ota_expected_offset = 0;
    _ota_bytes_written   = 0;
    _ota_running_crc     = 0xFFFFFFFF;
    _ota_seq             = 0;
    _ota_fopen_done      = false;
    _last_progress_pct   = 255;  // reset throttle so next session starts fresh
}

// ---------------------------------------------------------------------------
// _publish_ota_resp  — publish raw JSON string to ota/resp topic
// ---------------------------------------------------------------------------

void ModuleMqtt::_publish_ota_resp(const char *json)
{
    if (!_client || !json) return;
    const char *topic = _active_ota_resp_topic ? _active_ota_resp_topic : _ota_resp_topic;
    size_t len = strlen(json);
    pal_mqtt_client_publish(_client, topic, json, len, PAL_MQTT_QOS_1, false);
    LOG_MSG_INFO(MQTT_LOG, "ota/resp [%s]: %s", topic, json);
}

// ---------------------------------------------------------------------------
// _handle_ota_ctrl  — OTA control JSON on .../ota/ctrl
// ---------------------------------------------------------------------------

void ModuleMqtt::_handle_ota_ctrl(const pal_mqtt_message_t *m)
{
    // Parse: { "seq": N, "cmd": "ota_start|ota_abort|ota_complete", "data": {...} }
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, m->data, m->data_len);
    if (err) {
        LOG_MSG_WARNING(MQTT_LOG, "OTA ctrl parse error: %s", err.c_str());
        return;
    }

    uint32_t    seq = doc["seq"] | (uint32_t)0;
    const char *cmd = doc["cmd"] | "";
    if (cmd[0] == '\0') {
        LOG_MSG_WARNING(MQTT_LOG, "OTA ctrl: missing 'cmd' field");
        return;
    }

    LOG_MSG_INFO(MQTT_LOG, "OTA ctrl seq=%u cmd='%s'", (unsigned)seq, cmd);

    // ── ota_start ────────────────────────────────────────────────────────────
    if (strcmp(cmd, "ota_start") == 0) {
        if (_ota_state != MQTT_OTA_IDLE) {
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                     "\"code\":\"busy\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            return;
        }

        const char *target  = doc["data"]["target"]  | "main";
        uint32_t    size    = doc["data"]["size"]    | (uint32_t)0;
        const char *version = doc["data"]["version"] | "";

        uint8_t target_idx = _resolve_ota_target(target);
        if (target_idx == 0xFF) {
            LOG_MSG_ERROR(MQTT_LOG, "OTA: unknown target '%s'", target);
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                     "\"code\":\"unknown_target\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            return;
        }

        _ota_seq        = seq;
        _ota_target_idx = target_idx;
        _ota_total_size = size;
        _ota_state      = MQTT_OTA_REQUESTING_SESSION;

        MsgOtaStartRequest::Payload req{};
        req.target_idx = target_idx;
        strncpy(req.incoming_version, version, sizeof(req.incoming_version) - 1);
        hsys_msg_t *msg = MsgOtaStartRequest::create(id(), req);
        if (msg) {
            send(msg, MODULE_OTA_ID);
            LOG_MSG_INFO(MQTT_LOG, "OTA: MsgOtaStartRequest target=%u ver=%s → REQUESTING_SESSION",
                         (unsigned)target_idx, version);
        } else {
            LOG_MSG_ERROR(MQTT_LOG, "OTA: pool full — can't send MsgOtaStartRequest");
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_start\",\"status\":\"error\","
                     "\"code\":\"internal\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            _ota_reset();
        }
        return;
    }

    // ── ota_abort ────────────────────────────────────────────────────────────
    if (strcmp(cmd, "ota_abort") == 0) {
        if (_ota_state == MQTT_OTA_IDLE) {
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_abort\",\"status\":\"error\","
                     "\"code\":\"not_active\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            return;
        }
        if (_ota_fopen_done && _ota_driver) {
            _ota_driver->ferase(_ota_ctx);
        }
        MsgOtaAbortRequest::Payload ap{ OTA_ABORT_SOURCE_CANCELLED, {} };
        hsys_msg_t *am = MsgOtaAbortRequest::create(id(), ap);
        if (am) send(am, MODULE_OTA_ID);

        char resp[MODULE_MQTT_OTA_RESP_MAX];
        snprintf(resp, sizeof(resp),
                 "{\"seq\":%u,\"cmd\":\"ota_abort\",\"status\":\"ok\"}", (unsigned)seq);
        _publish_ota_resp(resp);
        _ota_reset();
        return;
    }

    // ── ota_complete ─────────────────────────────────────────────────────────
    if (strcmp(cmd, "ota_complete") == 0) {
        if (_ota_state != MQTT_OTA_ACTIVE) {
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"error\","
                     "\"code\":\"not_active\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            return;
        }

        // Optional CRC32 verification
        const char *crc_str   = doc["data"]["crc32"] | "";
        bool        do_crc    = (crc_str[0] != '\0');
        uint32_t    host_crc  = do_crc ? (uint32_t)strtoul(crc_str, nullptr, 16) : 0;
        uint32_t    local_crc = _ota_running_crc ^ 0xFFFFFFFFU;

        if (do_crc && host_crc != local_crc) {
            LOG_MSG_ERROR(MQTT_LOG, "OTA: CRC mismatch got=0x%08X expected=0x%08X",
                          (unsigned)local_crc, (unsigned)host_crc);
            if (_ota_driver) _ota_driver->ferase(_ota_ctx);
            MsgOtaCompleteNotify::Payload np{ false, {}, OTA_FS_ERR_WRITE_FAIL };
            hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), np);
            if (nm) send(nm, MODULE_OTA_ID);
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"error\","
                     "\"code\":\"crc_mismatch\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            _ota_reset();
            return;
        }

        // Commit the written image
        ota_fs_err_t close_err = _ota_driver->fclose(_ota_ctx);
        _ota_fopen_done = false;
        if (close_err != OTA_FS_OK) {
            LOG_MSG_ERROR(MQTT_LOG, "OTA: fclose failed err=%d", (int)close_err);
            MsgOtaCompleteNotify::Payload np{ false, {}, close_err };
            hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), np);
            if (nm) send(nm, MODULE_OTA_ID);
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"error\","
                     "\"code\":\"commit_failed\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            _ota_reset();
            return;
        }

        // Notify OtaModule — response will come via MsgOtaEvent
        MsgOtaCompleteNotify::Payload np{ true, {}, OTA_FS_OK };
        hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), np);
        if (nm) {
            send(nm, MODULE_OTA_ID);
            _ota_seq   = seq;
            _ota_state = MQTT_OTA_COMPLETING;
            LOG_MSG_INFO(MQTT_LOG, "OTA: fclose ok — waiting for MsgOtaEvent(COMPLETE)");
        } else {
            LOG_MSG_ERROR(MQTT_LOG, "OTA: pool full — can't send MsgOtaCompleteNotify");
            char resp[MODULE_MQTT_OTA_RESP_MAX];
            snprintf(resp, sizeof(resp),
                     "{\"seq\":%u,\"cmd\":\"ota_complete\",\"status\":\"error\","
                     "\"code\":\"internal\"}", (unsigned)seq);
            _publish_ota_resp(resp);
            _ota_reset();
        }
        return;
    }

    LOG_MSG_WARNING(MQTT_LOG, "OTA ctrl: unknown cmd '%s'", cmd);
}

// ---------------------------------------------------------------------------
// _handle_ota_data  — binary chunk on .../ota/data
// ---------------------------------------------------------------------------

void ModuleMqtt::_handle_ota_data(const pal_mqtt_message_t *m)
{
    if (_ota_state != MQTT_OTA_ACTIVE) {
        LOG_MSG_WARNING(MQTT_LOG, "OTA data: not ACTIVE (state=%d) — dropped", (int)_ota_state);
        return;
    }
    if (m->data_len < 5) {
        LOG_MSG_WARNING(MQTT_LOG, "OTA data: payload too small (%u bytes)", (unsigned)m->data_len);
        return;
    }

    // Extract 4-byte big-endian offset prefix
    const uint8_t *raw    = (const uint8_t *)m->data;
    uint32_t       offset = ((uint32_t)raw[0] << 24) |
                            ((uint32_t)raw[1] << 16) |
                            ((uint32_t)raw[2] <<  8) |
                             (uint32_t)raw[3];
    const uint8_t *chunk  = raw + 4;
    uint32_t       len    = (uint32_t)(m->data_len - 4);

    // Reject out-of-order chunks
    if (offset != _ota_expected_offset) {
        char resp[MODULE_MQTT_OTA_RESP_MAX];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"ota_chunk\",\"status\":\"error\","
                 "\"offset_expecting\":%u}", (unsigned)_ota_expected_offset);
        _publish_ota_resp(resp);
        LOG_MSG_WARNING(MQTT_LOG, "OTA chunk: out of order got=%u expected=%u",
                        (unsigned)offset, (unsigned)_ota_expected_offset);
        return;
    }

    // Write chunk to driver
    ota_fs_err_t err = _ota_driver->fwrite(_ota_ctx, chunk, len);
    if (err != OTA_FS_OK) {
        LOG_MSG_ERROR(MQTT_LOG, "OTA chunk: fwrite failed at offset=%u err=%d",
                      (unsigned)offset, (int)err);
        _ota_driver->ferase(_ota_ctx);
        MsgOtaAbortRequest::Payload ap{ OTA_ABORT_WRITE_ERROR, {} };
        hsys_msg_t *am = MsgOtaAbortRequest::create(id(), ap);
        if (am) send(am, MODULE_OTA_ID);
        char resp[MODULE_MQTT_OTA_RESP_MAX];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"ota_chunk\",\"status\":\"error\","
                 "\"offset_expecting\":%u}", (unsigned)offset);
        _publish_ota_resp(resp);
        _ota_reset();
        return;
    }

    // Update running CRC32 and counters
    _ota_running_crc     = crc32_update(_ota_running_crc, chunk, len);
    _ota_expected_offset += len;
    _ota_bytes_written   += len;

    // Publish progress notification (resets OtaModule inactivity timer)
    MsgOtaProgress::Payload pp{};
    pp.target_idx    = _ota_target_idx;
    pp.bytes_written = _ota_bytes_written;
    pp.total_bytes   = _ota_total_size;
    pp.percent       = (_ota_total_size > 0)
                       ? (uint8_t)((_ota_bytes_written * 100U) / _ota_total_size)
                       : 0;
    hsys_msg_t *pm = MsgOtaProgress::create(id(), pp);
    if (pm) publish(pm);

    // Respond with next expected offset
    char resp[MODULE_MQTT_OTA_RESP_MAX];
    snprintf(resp, sizeof(resp),
             "{\"cmd\":\"ota_chunk\",\"status\":\"ok\",\"offset_next\":%u}",
             (unsigned)_ota_expected_offset);
    _publish_ota_resp(resp);
}
