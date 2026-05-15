// ModuleUdpLog.h
//
// Routes log output to a UDP server over WiFi.
//
// Responsibilities:
//   1. Wait for MsgConfigReady — read log_udp_enabled / server_ip / port from
//      app_config_t and call pal_udp_log_init().
//   2. Wait for MsgWifiEvent(GOT_IP) — strip colons from MAC, call
//      pal_udp_log_start(), register pal_udp_log_sink with pal_logger.
//   3. On MsgWifiEvent(DISCONNECTED) — unregister sink, call pal_udp_log_stop().
//   4. On wake() (triggered by pal_udp_log_sink via wake callback) — drain
//      the queue by calling pal_udp_log_drain().
//
// Task placement: network_task (same as ModuleWifi so WiFi events arrive in
// order with no cross-task delay).

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"

class ModuleUdpLog : public HsysModule
{
public:
    ModuleUdpLog() : HsysModule(MODULE_UDP_LOG_ID, "udp_log") {}

    static ModuleUdpLog *instance();

    /** Called by the static C wake callback — routes to protected wake(). */
    void do_wake() { wake(); }

protected:
    void init()                               override;
    void on_msg_received(const hsys_msg_t &)  override;
    void on_wake()                            override;

private:
    void _try_start();
    void _stop();

    bool     m_config_ready = false;
    bool     m_udp_enabled  = false;
    bool     m_wifi_up      = false;
    bool     m_sink_active  = false;
    int32_t  m_sink_idx     = -1;

    char     m_ip[16]  = {};  ///< Server IP from config
    uint16_t m_port    = 0;   ///< Server port from config
    char     m_mac[13] = {};  ///< MAC without colons, e.g. "AABBCCDDEEFF"
};
