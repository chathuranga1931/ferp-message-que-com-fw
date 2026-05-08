// module_config.cpp
//
// ModuleConfig implementation.
//
// init():
//   Calls app_config_get_table() to obtain the config field table, then
//   calls hsys_config_init() to bind it to the module-owned config handle.
//
// On MSG_ID_SPIFFS_READY:
//   1. Allocate a 2 KB working buffer from the pool.
//   2. Read  Configs/DeviceConfigs.json into the buffer.
//   3. Parse JSON  → hsys_config_load_from_json copies matching values into
//                    the live app_config_t variables.
//   4. Serialise   → hsys_config_convert_to_json rebuilds canonical JSON
//                    (reusing the same buffer — parse data is already copied).
//   5. Overwrite   → app_spiffs_write_file replaces the file (not append).
//   6. Free        → return the buffer to the pool.
//   7. Publish     → MsgConfigReady signals that config is ready.

#include "module_config.h"
#include "app_spiffs.h"
#include "app.h"                  // app_config_get_table(), app_config_get()
#include "msg_spiffs_ready.h"
#include "msg_config_ready.h"
#include "msg_config_get_wifi.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_mqtt.h"
#include "msg_config_get_dt.h"
#include "msg_config_get_ota.h"
#include "msg_config_set.h"
#include "msg_config_wifi.h"
#include "msg_config_cloud.h"
#include "msg_config_mqtt.h"
#include "msg_config_dt.h"
#include "msg_config_ota.h"

#include "app_rootca.h"
#include "pal_logger.h"
#include <cstring>

#define __TAG__          "MOD_CONF"
#ifndef MOD_CONFIG_LOG_EN
#define MOD_CONFIG_LOG_EN true
#endif

// ── Static storage ────────────────────────────────────────────────────────────

static ModuleConfig s_instance;
ModuleConfig *ModuleConfig::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleConfig::init()
{
    // Bind the application config table to our private handle.
    uint16_t table_size = 0;
    config_t *table = app_config_get_table(&table_size);
    config_init_t cfg_init = { table_size, table };
    int32_t rc = hsys_config_init(cfg_init, &m_config_handle);
    if (rc != CONFIG_SUCCESS) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "hsys_config_init failed (%ld)", (long)rc);
    }

    subscribe(MSG_ID_SPIFFS_READY);
    subscribe(MSG_ID_CONFIG_SET);
    subscribe(MSG_ID_CONFIG_GET_WIFI);
    subscribe(MSG_ID_CONFIG_GET_CLOUD);
    subscribe(MSG_ID_CONFIG_GET_MQTT);
    subscribe(MSG_ID_CONFIG_GET_DT);
    subscribe(MSG_ID_CONFIG_GET_OTA);

    // Pre-allocate the JSON working buffer while the pool is fresh.
    // Doing this here — before any other module's init() runs — guarantees
    // the 2 K block is reserved. It is freed inside _load_and_save() after
    // the config file has been read and written back.
    m_json_buf = static_cast<char *>(hsys_pool_alloc(static_cast<uint16_t>(k_json_buf_size)));
    if (!m_json_buf) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "JSON buf alloc failed — config will use defaults");
    }

    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "config handle initialised, subscribed to SPIFFS_READY + CONFIG_SET + typed config gets");
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

        case MSG_ID_CONFIG_SET:
            _apply_config_set(MsgConfigSet::deserialize(msg));
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

        case MSG_ID_CONFIG_GET_OTA:
            _send_config_ota(MsgConfigGetOta::deserialize(msg).source_module_id);
            break;

        default:
            break;
    }
}

// ── Config load + save ────────────────────────────────────────────────────────

void ModuleConfig::_load_and_save()
{
    if (!m_config_handle.is_initialized) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "config handle not initialised");
    } else if (!m_json_buf) {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "no JSON buffer — skipping load, using defaults");
    } else {
        char *json_buf = m_json_buf;
        do {
                // ── 1. Read file ──────────────────────────────────────────────
                size_t bytes_read = 0;
                int32_t rc = app_spiffs_read_file(k_config_file,
                                                  json_buf,
                                                  k_json_buf_size,
                                                  &bytes_read,
                                                  5000);
                if (rc != APP_SPIFFS_OK || bytes_read < 10) {
                    if (rc != APP_SPIFFS_OK) {
                        LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                                        "config read failed (%ld) — using defaults", (long)rc);
                    } else {
                        LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                                        "config too small (%zu B) — using defaults", bytes_read);
                    }
                    // Fall through: save defaults to create / repair the file.
                } else {
                    // ── 2. Parse JSON → copy matching values into live config ─
                    json_buf[bytes_read] = '\0';
                    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "read %zu bytes — parsing", bytes_read);
                    rc = hsys_config_load_from_json(&m_config_handle, json_buf, bytes_read);
                    if (rc != CONFIG_SUCCESS) {
                        LOG_MSG_WARNING(MOD_CONFIG_LOG_EN,
                                        "JSON parse failed (%ld) — keeping defaults", (long)rc);
                    } else {
                        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "config loaded from file");
                    }
                }

                // ── 3. Serialise config table back to canonical JSON ──────────
                memset(json_buf, 0, k_json_buf_size);
                size_t json_len = 0;
                rc = hsys_config_convert_to_json(&m_config_handle, json_buf,
                                                 k_json_buf_size, &json_len);
                if (rc != CONFIG_SUCCESS || json_len == 0) {
                    LOG_MSG_ERROR(MOD_CONFIG_LOG_EN,
                                  "convert_to_json failed (%ld) — not saving", (long)rc);
                    break;
                }

                // ── 4. Overwrite file ─────────────────────────────────────────
                LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "saving %zu bytes to %s", json_len, k_config_file);
                rc = app_spiffs_write_file(k_config_file, json_buf, 5000);
                if (rc != APP_SPIFFS_OK) {
                    LOG_MSG_ERROR(MOD_CONFIG_LOG_EN,
                                  "write failed (%ld) — config not persisted", (long)rc);
                } else {
                    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "config saved OK");
                }

            } while (false);

        // Release the buffer back to the pool — it was a one-shot use.
        hsys_pool_free(m_json_buf);
        m_json_buf = nullptr;
    }

    // ── 5. Publish MsgConfigReady — always, even if load/save failed ──────────
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
    p.root_ca       = root_ca;   // pointer to static PEM string in app_rootca.h
    p.hb_enabled    = cfg->cloud_hb_enabled;
    p.hb_interval_s = cfg->cloud_hb_interval_s;

    hsys_msg_t *out = MsgConfigCloud::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigCloud -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  root_ca      = %s", p.root_ca ? "***" : "(null)");
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

void ModuleConfig::_send_config_ota(hsys_module_id_t requester)
{
    const app_config_t *cfg = app_config_get();
    if (!cfg) return;

    MsgConfigOta::Payload p{};
    strncpy(p.server_url,  cfg->ota_server_url, sizeof(p.server_url)  - 1);
    p.root_ca          = root_ca;   // pointer to static PEM string in app_rootca.h
    p.check_interval_s = cfg->ota_check_interval_s;

    hsys_msg_t *out = MsgConfigOta::create(id(), p);
    if (out) {
        out->receiver_id = requester;
        send(out, requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigOta -> module %u:", (unsigned)requester);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  server_url       = \"%s\"", p.server_url);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  check_interval_s = %u",     (unsigned)p.check_interval_s);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "  root_ca          = %s", p.root_ca ? "***" : "(null)");
    }
}

// ── MsgConfigSet handler ──────────────────────────────────────────────────────

void ModuleConfig::_apply_config_set(const MsgConfigSet::Payload &p)
{
    uint16_t table_size = 0;
    config_t *table = app_config_get_table(&table_size);

    // Find the entry matching the key name
    for (uint16_t i = 0; i < table_size; i++) {
        if (strncmp(table[i].name, p.key, MsgConfigSet::KEY_MAX_LEN) != 0) continue;

        // Apply the value directly into the live config struct pointer
        switch (p.type) {
            case HSYS_TYPE_STRING:
                strncpy(static_cast<char *>(table[i].p_global_value),
                        p.value.as_str,
                        table[i].max_length - 1);
                static_cast<char *>(table[i].p_global_value)[table[i].max_length - 1] = '\0';
                LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "ConfigSet: %s = \"%s\"", p.key, p.value.as_str);
                break;
            case HSYS_TYPE_UINT32:
                *static_cast<uint32_t *>(table[i].p_global_value) = p.value.as_uint32;
                LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "ConfigSet: %s = %lu", p.key, (unsigned long)p.value.as_uint32);
                break;
            case HSYS_TYPE_BOOL:
                *static_cast<bool *>(table[i].p_global_value) = p.value.as_bool;
                LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "ConfigSet: %s = %s", p.key, p.value.as_bool ? "true" : "false");
                break;
            default:
                LOG_MSG_WARNING(MOD_CONFIG_LOG_EN, "ConfigSet: unknown type %d for key '%s'", (int)p.type, p.key);
                return;
        }

        // Persist to SPIFFS using a pool buffer
        char *buf = static_cast<char *>(hsys_pool_alloc(static_cast<uint16_t>(k_json_buf_size)));
        if (!buf) {
            LOG_MSG_WARNING(MOD_CONFIG_LOG_EN, "ConfigSet: no pool buf — value applied but not saved");
            return;
        }
        size_t json_len = 0;
        int32_t rc = hsys_config_convert_to_json(&m_config_handle, buf, k_json_buf_size, &json_len);
        if (rc == CONFIG_SUCCESS && json_len > 0) {
            rc = app_spiffs_write_file(k_config_file, buf, 5000);
            if (rc == APP_SPIFFS_OK) {
                LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "ConfigSet: saved %zu bytes", json_len);
            } else {
                LOG_MSG_WARNING(MOD_CONFIG_LOG_EN, "ConfigSet: SPIFFS write failed (%ld)", (long)rc);
            }
        } else {
            LOG_MSG_WARNING(MOD_CONFIG_LOG_EN, "ConfigSet: json convert failed (%ld)", (long)rc);
        }
        hsys_pool_free(buf);
        return;
    }

    LOG_MSG_WARNING(MOD_CONFIG_LOG_EN, "ConfigSet: unknown key '%s'", p.key);
}
