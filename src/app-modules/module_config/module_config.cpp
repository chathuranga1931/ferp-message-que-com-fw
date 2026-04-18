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
#include "app.h"                  // app_config_get_handle()
#include "msg_spiffs_ready.h"
#include "msg_config_ready.h"
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
    LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "subscribed to MSG_ID_SPIFFS_READY");
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
            LOG_MSG_WARN(MOD_CONFIG_LOG_EN,
                         "config file read failed (%ld) — using defaults", (long)rc);
        } else {
            LOG_MSG_WARN(MOD_CONFIG_LOG_EN,
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
            LOG_MSG_WARN(MOD_CONFIG_LOG_EN,
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
    // _app_config is the live instance owned by app.cpp.
    extern app_config_t _app_config;
    hsys_msg_t *out = MsgConfigReady::create(id(), &_app_config);
    if (out) {
        publish(out);
        LOG_MSG_INFO(MOD_CONFIG_LOG_EN, "MsgConfigReady published");
    } else {
        LOG_MSG_ERROR(MOD_CONFIG_LOG_EN, "failed to create MsgConfigReady");
    }
}
