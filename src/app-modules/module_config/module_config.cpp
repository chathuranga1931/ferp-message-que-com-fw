// module_config.cpp
//
// ModuleConfig implementation.
//
// On MSG_ID_SPIFFS_READY:
//   1. Read  Configs/DeviceConfigs.json
//   2. Parse JSON  → hsys_config_load_from_json iterates every key in the
//                    config table and copies matching values into the live
//                    app_config_t variables.
//   3. Serialise   → hsys_config_convert_to_json rebuilds a canonical JSON
//                    from the (now-updated) config table.
//   4. Overwrite   → app_spiffs_write_file replaces the file (not append).
//   5. Publish     → MsgConfigReady carries a const pointer to the live config.

#include "module_config.h"
#include "app_spiffs.h"
#include "app.h"                  // app_config_get_handle(), app_config_get()
#include "msg_spiffs_ready.h"
#include "msg_config_ready.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_dt.h"
#include "msg_config_wifi.h"
#include "msg_config_cloud.h"
#include "msg_config_mqtt.h"
#include "msg_config_dt.h"
#include "pal_logger.h"
#include <cstring>

#define __TAG__          "MOD_CONF"
#ifndef MOD_CONFIG_LOG_EN
#define MOD_CONFIG_LOG_EN true
#endif

// ── Static storage ────────────────────────────────────────────────────────────

static ModuleConfig s_instance;
ModuleConfig *ModuleConfig::instance() { return &s_instance; }

// Static JSON buffer shared across _load_and_save() calls — avoids blowing
// the task stack with a 4 KB local array.
char ModuleConfig::s_json_buf[ModuleConfig::k_json_buf_size];

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleConfig::init()
{
    subscribe(MSG_ID_SPIFFS_READY);
    subscribe(MSG_ID_CONFIG_GET_WIFI);
    subscribe(MSG_ID_CONFIG_GET_CLOUD);
    subscribe(MSG_ID_CONFIG_GET_MQTT);
    subscribe(MSG_ID_CONFIG_GET_DT);
    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "subscribed to SPIFFS_READY + typed config gets");
}

// ── Message handler ───────────────────────────────────────────────────────────

void ModuleConfig::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_SPIFFS_READY:
            LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "SPIFFS ready — loading config");
            _load_and_save();
            break;

        case MSG_ID_CONFIG_GET_WIFI:
            _send_config_wifi(MsgConfigGetWifi::deserialize(msg).source_module_id);
            break;

        case MSG_ID_CONFIG_GET_CLOUD:
            _send_config_cloud(MsgConfigGetCloud::deserialize(msg).source_module_id);
            break;

        case MSG_ID_CONFIG_GET_MQTT:
            _send_config_mqtt(MsgConfigGetMqtt::deserialize(msg).source_module_id);
            break;

        case MSG_ID_CONFIG_GET_DT:
            _send_config_dt(MsgConfigGetDT::deserialize(msg).source_module_id);
            break;

        default:
            break;
    }
}

// ── Config load + save ────────────────────────────────────────────────────────

void ModuleConfig::_load_and_save()
{
    config_handle_t *hndl = app_config_get_handle();
    if (!hndl || !hndl->is_initialized) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "config handle not initialised");
        return;
    }

    // ── 1. Read file ──────────────────────────────────────────────────────────
    size_t bytes_read = 0;
    int32_t rc = app_spiffs_read_file(k_config_file,
                                      s_json_buf,
                                      k_json_buf_size,
                                      &bytes_read,
                                      5000);

    if (rc != APP_SPIFFS_OK || bytes_read < 10) {
        if (rc != APP_SPIFFS_OK) {
            LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                         "config file read failed (%ld) — using defaults", (long)rc);
        } else {
            LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                         "config file too small (%zu bytes) — using defaults", bytes_read);
        }
        // Fall through: save defaults to create / repair the file.
    }
    else {
        // ── 2. Parse JSON → load matching keys into live config variables ─────
        s_json_buf[bytes_read] = '\0';   // guarantee null termination
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "read %zu bytes — parsing", bytes_read);

        rc = hsys_config_load_from_json(hndl, s_json_buf, bytes_read);
        if (rc != CONFIG_SUCCESS) {
            LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                         "JSON parse failed (%ld) — keeping defaults", (long)rc);
        } else {
            LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "config loaded from file");
        }
    }

    // ── 3. Serialise config table back to canonical JSON ──────────────────────
    memset(s_json_buf, 0, k_json_buf_size);
    size_t json_len = 0;

    rc = hsys_config_convert_to_json(hndl, s_json_buf, k_json_buf_size, &json_len);
    if (rc != CONFIG_SUCCESS || json_len == 0) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN,
                      "convert_to_json failed (%ld) — not saving", (long)rc);
        goto publish;
    }

    // ── 4. Overwrite file (write, not append) ─────────────────────────────────
    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "saving %zu bytes to %s", json_len, k_config_file);

    rc = app_spiffs_write_file(k_config_file, s_json_buf, 5000);
    if (rc != APP_SPIFFS_OK) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN,
                      "write failed (%ld) — config not persisted", (long)rc);
    } else {
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "config saved OK");
    }

publish:
    // ── 5. Publish MsgConfigReady — always, even if save failed ──────────────
    hsys_msg_t *out = MsgConfigReady::create(id());
    if (out) {
        publish(out);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigReady published");
    } else {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "failed to create MsgConfigReady");
    }
}

// ── Typed domain config response senders ─────────────────────────────────────

void ModuleConfig::_send_config_wifi(hsys_module_id_t requester)
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    MsgConfigWifi::Payload p{};
    strncpy(p.ssid,     cfg->wifi_ssid,     sizeof(p.ssid)     - 1);
    strncpy(p.password, cfg->wifi_password, sizeof(p.password) - 1);

    hsys_msg_t *out = MsgConfigWifi::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigWifi -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  ssid     = \"%s\"", p.ssid);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  password = %s", (p.password[0] != '\0') ? "***" : "(empty)");
    }
}

void ModuleConfig::_send_config_cloud(hsys_module_id_t requester)
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    MsgConfigCloud::Payload p{};
    strncpy(p.url,           cfg->cloud_url,       sizeof(p.url)           - 1);
    strncpy(p.secret,        cfg->cloud_secret,    sizeof(p.secret)        - 1);
    strncpy(p.uuid,          cfg->device_uuid,     sizeof(p.uuid)          - 1);
    strncpy(p.wifi_ssid,     cfg->wifi_ssid,       sizeof(p.wifi_ssid)     - 1);
    strncpy(p.wifi_password, cfg->wifi_password,   sizeof(p.wifi_password) - 1);
    p.hb_enabled    = cfg->cloud_hb_enabled;
    p.hb_interval_s = cfg->cloud_hb_interval_s;

    hsys_msg_t *out = MsgConfigCloud::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigCloud -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  url          = \"%s\"", p.url);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  uuid         = \"%s\"", p.uuid);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  secret       = %s", (p.secret[0] != '\0') ? "***" : "(empty)");
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  wifi_ssid    = \"%s\"", p.wifi_ssid);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  hb_enabled   = %d", (int)p.hb_enabled);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  hb_interval  = %us", (unsigned)p.hb_interval_s);
    }
}

void ModuleConfig::_send_config_mqtt(hsys_module_id_t requester)
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    MsgConfigMqtt::Payload p{};
    strncpy(p.host,     cfg->mqtt_host,     sizeof(p.host)     - 1);
    strncpy(p.user,     cfg->mqtt_user,     sizeof(p.user)     - 1);
    strncpy(p.password, cfg->mqtt_password, sizeof(p.password) - 1);
    p.port = cfg->mqtt_port;

    hsys_msg_t *out = MsgConfigMqtt::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigMqtt -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  host     = \"%s\"", p.host);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  port     = %u", (unsigned)p.port);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  user     = \"%s\"", p.user);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  password = %s", (p.password[0] != '\0') ? "***" : "(empty)");
    }
}

void ModuleConfig::_send_config_dt(hsys_module_id_t requester)
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    MsgConfigDT::Payload p{};
    p.display_type        = cfg->display_type;
    p.stabilize_delay_ms  = cfg->stabilize_delay_ms;
    p.printer_copy_count  = cfg->printer_copy_count;
    strncpy(p.printer_url, cfg->printer_url, sizeof(p.printer_url) - 1);

    hsys_msg_t *out = MsgConfigDT::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigDT -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  display_type       = %u", (unsigned)p.display_type);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  stabilize_delay_ms = %u", (unsigned)p.stabilize_delay_ms);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  printer_url        = \"%s\"", p.printer_url);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  printer_copy_count = %u", (unsigned)p.printer_copy_count);
    }
}
