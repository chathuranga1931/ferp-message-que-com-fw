// module_config.h
//
// ModuleConfig — HSYS module that owns the device configuration lifecycle.
//
// Lifecycle:
//   init()              — subscribe to MSG_ID_SPIFFS_READY
//   on_msg_received()   — on SPIFFS_READY:
//                           1. Read Configs/DeviceConfigs.json via app_spiffs
//                           2. Parse JSON → load matching keys into config table
//                           3. Serialise config table back → overwrite the file
//                           4. Publish MsgConfigReady with pointer to live config
//
// Subscribers (e.g. ModuleWifi, ModuleMqtt) receive MsgConfigReady and
// copy the fields they need during their own on_msg_received().

#pragma once

#include "hsys_module.h"
#include "hsys_config.h"     // config_handle_t

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_CONFIG_ID    ((hsys_module_id_t)6)
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

    // ── JSON buffer (static — kept off the stack) ────────────────────────────
    static constexpr size_t k_json_buf_size = 4096;
    static char s_json_buf[k_json_buf_size];

    // ── Internal helpers ─────────────────────────────────────────────────────
    void _load_and_save();
};
