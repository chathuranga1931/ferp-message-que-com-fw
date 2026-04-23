#pragma once
/**
 * @file module_sim_bridge.h
 * @brief Simulator-only HSYS module — subscribes to app messages and forwards
 *        them to the Python UI via mac_driver_send_json().
 *
 * All TCP I/O is owned by mac_driver (pal/mac-pc/mac_driver.cpp).
 * This module is purely responsible for:
 *   - Subscribing to HSYS messages that the UI wants to visualise.
 *   - Serialising those messages to JSON and calling mac_driver_send_json().
 *   - Caching system state so it can be replayed when the UI reconnects.
 *
 * This module must never be compiled for the ESP-IDF target.
 */

#include "hsys_module.h"
#include "app_module_ids.h"

#include <atomic>
#include <thread>

class ModuleSimBridge : public HsysModule
{
public:
    static constexpr hsys_module_id_t MODULE_ID = MODULE_SIM_BRIDGE_ID;

    ModuleSimBridge() : HsysModule(MODULE_ID, "sim_bridge") {}

    static ModuleSimBridge *instance();

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

    static void on_ui_connected();   ///< called by mac_driver on each new client

private:
    void _send_json(const char *id, const char *data_json);
    void _send_pool_status();

    std::atomic<bool> _pool_stop{false};
    std::thread        _pool_thread;

    bool _spiffs_ready = false;

    // ── Cached state for UI-reconnect replay ──────────────────────────────
    char _last_wifi_json[256]     = {};
    char _last_internet_json[64]  = {};
    char _last_cloud_json[128]    = {};
};

#define SIM_BRIDGE_MODULE_ID  ModuleSimBridge::MODULE_ID
