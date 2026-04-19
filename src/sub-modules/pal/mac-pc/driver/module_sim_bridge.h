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

class ModuleSimBridge : public HsysModule
{
public:
    static constexpr uint8_t MODULE_ID = 20;

    ModuleSimBridge() : HsysModule(MODULE_ID, "sim_bridge") {}

    static ModuleSimBridge *instance();

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    void _send_json(const char *id, const char *data_json);
    void _send_pool_status();

    static constexpr uint32_t POOL_REPORT_INTERVAL_TICKS = 5;

    uint32_t _tick_count   = 0;
    bool     _spiffs_ready = false;
};

#define SIM_BRIDGE_MODULE_ID  ModuleSimBridge::MODULE_ID
