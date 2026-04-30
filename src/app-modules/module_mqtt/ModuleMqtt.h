// ModuleMqtt.h
//
// ModuleMqtt — MQTT broker client module.
//
// Bridges the HSYS message bus to an MQTT broker via pal_mqtt_*.
//
// State machine:
//
//   WAIT_CONFIG ──[MsgConfigMqtt]──────────────────────► WAIT_INTERNET
//   WAIT_INTERNET ──[MsgInternetStatus(connected)]──────► CONNECTING
//   CONNECTING ──[PAL_MQTT_EVENT_CONNECTED]─────────────► CONNECTED
//   CONNECTED ──[PAL_MQTT_EVENT_DISCONNECTED]───────────► CONNECTING  (auto-reconnect)
//   CONNECTED ──[MsgInternetStatus(!connected)]──────────► WAIT_INTERNET
//
// Inbound (cmd topic → HSYS bus):
//   DATA event → parse envelope → app_msg_codec_decode() → send/publish on bus
//
// Outbound (HSYS bus → resp / evt topic):
//   Subscribe to encodeable HSYS messages → app_msg_codec_encode() → pal_mqtt_client_publish()
//   Responses (DIRECT replies to this module) → resp topic
//   Notifications (unsolicited events)        → evt topic

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "pal_mqtt.h"
#include <stdint.h>

#define MODULE_MQTT_NAME "mqtt"

// Maximum sizes for topic strings and JSON envelope
#define MODULE_MQTT_TOPIC_MAX    256
#define MODULE_MQTT_MSG_NAME_MAX  48
#define MODULE_MQTT_DATA_MAX     512
#define MODULE_MQTT_ENV_MAX      640   ///< Full envelope = {"seq":...,"msg":"...","data":{...}}

// ---------------------------------------------------------------------------
// ModuleMqtt
// ---------------------------------------------------------------------------

class ModuleMqtt : public HsysModule
{
public:
    ModuleMqtt() : HsysModule(MODULE_MQTT_ID, MODULE_MQTT_NAME) {}

    static ModuleMqtt *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    // ── State machine ─────────────────────────────────────────────────────────
    typedef enum {
        STATE_WAIT_CONFIG,
        STATE_WAIT_INTERNET,
        STATE_CONNECTING,
        STATE_CONNECTED,
    } mqtt_state_t;

    mqtt_state_t _state = STATE_WAIT_CONFIG;

    // ── PAL handle ────────────────────────────────────────────────────────────
    pal_mqtt_client_handle_t _client = nullptr;

    // ── Topics ────────────────────────────────────────────────────────────────
    char _cmd_topic[MODULE_MQTT_TOPIC_MAX]  = {};  ///< subscribe (host → device)
    char _resp_topic[MODULE_MQTT_TOPIC_MAX] = {};  ///< publish (response to cmd)
    char _evt_topic[MODULE_MQTT_TOPIC_MAX]  = {};  ///< publish (unsolicited events)

    // ── Sequence tracking ─────────────────────────────────────────────────────
    uint32_t _last_cmd_seq  = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void _on_config_mqtt(const hsys_msg_t &msg);
    void _on_internet_status(const hsys_msg_t &msg);
    void _on_pal_connected();
    void _on_pal_disconnected();
    void _on_pal_data(const pal_mqtt_message_t *m);
    void _on_outbound_msg(const hsys_msg_t &msg, bool is_evt);
    void _build_topics(const char *dev_type, const char *group, const char *device_id);
    void _publish_envelope(const char *topic, uint32_t seq,
                           const char *msg_name, const char *data_json);

    // PAL event callback (static trampoline)
    static void s_pal_event_cb(pal_mqtt_event_data_t *ev);
};
