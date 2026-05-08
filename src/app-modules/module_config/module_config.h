#pragma once

#include "hsys_module.h"
#include "hsys_config.h"       // config_handle_t, config_t
#include "hsys_pool.h"         // hsys_pool_alloc / hsys_pool_free
#include "msg_config_set.h"    // MsgConfigSet
#include "msg_config_get_key.h" // MsgConfigGetKey
#include "msg_config_value.h"   // MsgConfigValue

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_CONFIG_NAME  "config"

// ---------------------------------------------------------------------------
// ModuleConfig
// ---------------------------------------------------------------------------

class ModuleConfig : public HsysModule
{
public:
    ModuleConfig() : HsysModule(MODULE_CONFIG_ID, MODULE_CONFIG_NAME) {}

    static ModuleConfig *instance();

protected:
    void init()     override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── Config file constants ────────────────────────────────────────────────
    static constexpr const char *k_config_dir  = "Configs";
    static constexpr const char *k_config_file = "Configs/DeviceConfigs.json";

    // ── JSON working buffer size (pool-allocated on demand, not static) ──────
    static constexpr size_t k_json_buf_size = 2048;

    // ── Config handle (owned here; initialised in init()) ───────────────────
    config_handle_t m_config_handle {};

    // ── JSON working buffer — allocated once in init(), freed after load ─────
    // Allocated early (before any other module can consume pool blocks) so
    // _load_and_save() is guaranteed a buffer when SPIFFS_READY fires.
    char *m_json_buf = nullptr;

    // ── Internal helpers ─────────────────────────────────────────────────────
    void _load_and_save();
    void _apply_config_set   (const hsys_msg_t &msg);
    void _send_config_value  (uint16_t key, hsys_module_id_t requester);
    void _send_config_wifi   (hsys_module_id_t requester);
    void _send_config_cloud  (hsys_module_id_t requester);
    void _send_config_mqtt   (hsys_module_id_t requester);
    void _send_config_dt     (hsys_module_id_t requester);
    void _send_config_ota    (hsys_module_id_t requester);
};
