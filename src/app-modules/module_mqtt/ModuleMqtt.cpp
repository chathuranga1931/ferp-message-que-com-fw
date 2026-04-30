// ModuleMqtt.cpp
//
// ModuleMqtt implementation — see ModuleMqtt.h for architecture notes.

#include "ModuleMqtt.h"

#include "pal_logger.h"
#include "app_msg_codec.h"
#include "hsys_msg.h"
#include "app_config.h"

// Messages this module subscribes to
#include "msg_config_ready.h"
#include "msg_config_mqtt.h"
#include "msg_config_wifi.h"
#include "msg_config_get_mqtt.h"
#include "msg_internet_status.h"
#include "msg_fuel_pumped.h"
#include "msg_nozzle_state.h"
#include "msg_sensor_data.h"
#include "msg_ota_event.h"
#include "msg_ota_progress.h"
#include "msg_config_cloud.h"
#include "msg_config_ota.h"
#include "msg_mqtt_status.h"

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
    subscribe(MsgFuelPumped::ID);    // NOTIFICATION → evt
    subscribe(MsgNozzleState::ID);   // NOTIFICATION → evt
    subscribe(MsgSensorData::ID);    // NOTIFICATION → evt
    subscribe(MsgOtaEvent::ID);      // NOTIFICATION → evt
    subscribe(MsgOtaProgress::ID);   // NOTIFICATION → evt

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

        // ── Outbound NOTIFICATION events ───────────────────────────────────
        case MsgFuelPumped::ID:
        case MsgNozzleState::ID:
        case MsgSensorData::ID:
        case MsgOtaEvent::ID:
        case MsgOtaProgress::ID:
            if (_state == STATE_CONNECTED) {
                _on_outbound_msg(msg, true /*evt*/);
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

    // Build topic paths — we need device_uuid and device_group from somewhere.
    // They are baked into the config via app.cpp defaults; we read them from
    // msg payload context by looking at the broker_uri pattern.
    // For topic we need the app_config directly — use extern reference.
    extern app_config_t _app_config;  // defined in app.cpp
    char dev_id[40];
    uuid_to_topic_id(_app_config.device_uuid, dev_id, sizeof(dev_id));
    _build_topics("ferp-com", _app_config.device_group, dev_id);

    // Use device_uuid as client ID (truncated to fit)
    strncpy(cfg.client_id, _app_config.device_uuid, sizeof(cfg.client_id) - 1);

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
    LOG_MSG_INFO(MQTT_LOG, "config received — broker=%s:%u  state=WAIT_INTERNET",
                 p.host, p.port);
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
        MsgMqttStatus::Payload sp{ .connected = true };
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

    LOG_MSG_INFO(MQTT_LOG, "cmd topic: %s", _cmd_topic);
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
        MsgMqttStatus::Payload sp{ .connected = false };
        hsys_msg_t *m = MsgMqttStatus::create(id(), sp);
        if (m) publish(m);
    }
}

// ---------------------------------------------------------------------------
// _on_pal_data  — inbound cmd from broker
// ---------------------------------------------------------------------------

void ModuleMqtt::_on_pal_data(const pal_mqtt_message_t *m)
{
    if (!m || !m->data || m->data_len == 0) return;

    // Parse envelope: { "seq": N, "msg": "...", "data": {...} }
    StaticJsonDocument<MODULE_MQTT_ENV_MAX> doc;
    DeserializationError err = deserializeJson(doc, m->data, m->data_len);
    if (err) {
        LOG_MSG_WARNING(MQTT_LOG, "envelope parse error: %s", err.c_str());
        return;
    }

    uint32_t    seq      = doc["seq"] | (uint32_t)0;
    const char *msg_name = doc["msg"] | "";
    if (msg_name[0] == '\0') {
        LOG_MSG_WARNING(MQTT_LOG, "envelope missing 'msg' field");
        return;
    }

    // If the cmd arrived on a wildcard/group topic (not the exact device cmd
    // topic), only process it when multicast_resp is enabled for this message
    // type.  With the current single-subscription model this is never triggered,
    // but it is kept as a safety guard for future group-topic support.
    bool exact_topic = (m->topic_len == strlen(_cmd_topic) &&
                        strncmp(m->topic, _cmd_topic, m->topic_len) == 0);
    if (!exact_topic && !app_msg_codec_is_multicast(msg_name)) {
        LOG_MSG_INFO(MQTT_LOG, "cmd '%s' via wildcard topic — multicast_resp=false, ignored", msg_name);
        return;
    }

    // Serialize the "data" sub-object back to JSON string for the codec
    char data_json[MODULE_MQTT_DATA_MAX];
    if (doc.containsKey("data")) {
        serializeJson(doc["data"], data_json, sizeof(data_json));
    } else {
        strncpy(data_json, "{}", sizeof(data_json));
    }

    _last_cmd_seq = seq;

    LOG_MSG_INFO(MQTT_LOG, "cmd seq=%u msg='%s'", seq, msg_name);

    // Decode into an HSYS message
    hsys_msg_t *decoded = app_msg_codec_decode(msg_name, data_json, id());
    if (!decoded) {
        LOG_MSG_WARNING(MQTT_LOG, "unknown or undecodeable msg: %s", msg_name);
        return;
    }

    // Route: DIRECT to specific module or NOTIFICATION broadcast
    hsys_module_id_t dest = app_msg_codec_get_dest(msg_name);
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

    const char *topic = is_evt ? _evt_topic : _resp_topic;
    _publish_envelope(topic, seq, msg_name, data_json);
}

// ---------------------------------------------------------------------------
// _build_topics
// ---------------------------------------------------------------------------

void ModuleMqtt::_build_topics(const char *dev_type, const char *group,
                                 const char *device_id)
{
    snprintf(_cmd_topic,  sizeof(_cmd_topic),
             "ferp/%s/%s/%s/cmd",  dev_type, group, device_id);
    snprintf(_resp_topic, sizeof(_resp_topic),
             "ferp/%s/%s/%s/resp", dev_type, group, device_id);
    snprintf(_evt_topic,  sizeof(_evt_topic),
             "ferp/%s/%s/%s/evt",  dev_type, group, device_id);
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
