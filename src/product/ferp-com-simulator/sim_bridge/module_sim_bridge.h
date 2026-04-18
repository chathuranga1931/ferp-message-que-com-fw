#pragma once
/**
 * @file module_sim_bridge.h
 * @brief Simulator-only module: subscribes to every UI-visible message and
 *        serialises it as a JSON line to a TCP socket (port 9000 by default).
 *
 * The Python UI connects to this socket and renders the hardware state.
 * This module exists ONLY in the ferp-com-simulator product — it must never
 * be compiled for the ESP-IDF target.
 *
 * JSON format (one line per event, terminated with '\n'):
 *   {"id":"MSG_WIFI_EVENT","ts":1234,"data":{...}}
 *
 * Inbound commands from the Python UI:
 *   {"id":"SIM_BTN","data":{"btn":"print1_short"}}
 *   {"id":"SIM_MQTT_INJECT","data":{"topic":"...","payload":"..."}}
 *   {"id":"SIM_OTA_TRIGGER","data":{"driver":0}}
 */

#include "hsys_module.h"
#include "hsys_mutex.h"     // hsys_mutex_handle_t

class ModuleSimBridge : public HsysModule
{
public:
    static constexpr uint8_t MODULE_ID = 20;

    // HsysModule stores id/name — no virtual override needed
    ModuleSimBridge()
        : HsysModule(MODULE_ID, "sim_bridge") {}

    static ModuleSimBridge *instance();

    /**
     * @brief Start the TCP server on the given port.
     *        Called once from main() before sim_app_init().
     */
    void start_server(uint16_t port = 9000);

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    void _accept_loop();            ///< runs in a background std::thread
    void _read_loop(int client_fd); ///< reads commands from the Python UI
    void _send_json(const char *id, const char *data_json);
    void _send_pool_status();       ///< samples pool + msg-header stats → SIM_POOL_STATUS
    void _send_state_snapshot();    ///< replays cached system state to a newly connected client

    /** Pool status is sent every N ticks (seconds). */
    static constexpr uint32_t POOL_REPORT_INTERVAL_TICKS = 5;

    int      _server_fd   = -1;
    int      _client_fd   = -1;      ///< only one UI client at a time
    uint16_t _port        = 9000;
    uint32_t _tick_count  = 0;       ///< counts MSG_TICK_1000MS ticks

    // ── State snapshot — replayed to any new UI client on connect ────────────
    bool     _spiffs_ready = false;

    // protect _client_fd from concurrent writer + reader threads
    hsys_mutex_handle_t _mutex = nullptr;
};

/** Convenience macro for task-table use */
#define SIM_BRIDGE_MODULE_ID  ModuleSimBridge::MODULE_ID
