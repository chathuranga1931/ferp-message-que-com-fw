// ModuleUdpLog.cpp
//
// See ModuleUdpLog.h for design notes.

#include "ModuleUdpLog.h"

#include "msg_config_ready.h"
#include "msg_wifi_event.h"
#include "app.h"             // app_config_get()
#include "app_config.h"
#include "pal_logger.h"      // pal_logger_register_sink / unregister_sink
#include "pal_udp_log.h"
#include "hsys_task_mgr.h"   // hsys_task_mgr_wake_module() — used in udp_log_wake

#include <string.h>

#define __TAG__  "MODUDP  "
// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleUdpLog s_instance;
ModuleUdpLog *ModuleUdpLog::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Wake callback — called from pal_udp_log_sink() under the log mutex.
// IMPORTANT: Must NOT call HsysModule::wake() — on failure that calls
// log_error(), which re-enters pal_logger_log() and deadlocks on s_log_mutex.
// Call hsys_task_mgr_wake_module() directly: non-blocking, silent on full.
// ---------------------------------------------------------------------------

static void udp_log_wake(void)
{
    hsys_task_mgr_wake_module(ModuleUdpLog::instance()->id());
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ModuleUdpLog::init()
{
    log("init");
    subscribe(MsgConfigReady::ID);
    subscribe(MsgWifiEvent::ID);
}

// ---------------------------------------------------------------------------
// Message handler
// ---------------------------------------------------------------------------

void ModuleUdpLog::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

    case MsgConfigReady::ID: {
        const app_config_t *cfg = app_config_get();
        if (!cfg) break;

        m_udp_enabled = cfg->log_udp_enabled;
        strncpy(m_ip, cfg->log_udp_server_ip, sizeof(m_ip) - 1);
        m_ip[sizeof(m_ip) - 1] = '\0';
        m_port         = (uint16_t)cfg->log_udp_port;
        m_config_ready = true;

        LOG_MSG_DEBUG(true, "config: enabled=%d  server=%s:%u", m_udp_enabled, m_ip, m_port);

        // Pre-initialise driver (MAC may not be known yet; it will be
        // updated in _try_start() once WiFi has an IP).
        if (m_udp_enabled) {
            pal_udp_log_init(m_ip, m_port,
                             m_mac[0] ? m_mac : nullptr,
                             udp_log_wake);
        }

        // WiFi may already be up (e.g. after a live config change).
        if (m_wifi_up) _try_start();
        break;
    }

    case MsgWifiEvent::ID: {
        auto p = MsgWifiEvent::deserialize(msg);

        if (p.event == WIFI_EVENT_STA_GOT_IP) {
            // Strip colons from "XX:XX:XX:XX:XX:XX" → "XXXXXXXXXXXX"
            int out = 0;
            for (int i = 0; p.mac_address[i] != '\0' && out < 12; i++) {
                if (p.mac_address[i] != ':') {
                    m_mac[out++] = p.mac_address[i];
                }
            }
            m_mac[out] = '\0';

            m_wifi_up = true;
            _try_start();

        } else if (p.event == WIFI_EVENT_STA_DISCONNECTED) {
            m_wifi_up = false;
            LOG_MSG_DEBUG(true, "STOP on WiFi disconnect");
            _stop();
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Wake handler — drain UDP queue
// ---------------------------------------------------------------------------

void ModuleUdpLog::on_wake()
{
    pal_udp_log_drain();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void ModuleUdpLog::_try_start()
{
    if (!m_config_ready || !m_udp_enabled || !m_wifi_up || m_sink_active) return;

    // Re-init with the now-known MAC address
    pal_udp_log_init(m_ip, m_port, m_mac[0] ? m_mac : nullptr, udp_log_wake);
    pal_udp_log_start();

    m_sink_idx    = pal_logger_register_sink(pal_udp_log_sink);
    m_sink_active = (m_sink_idx >= 0);

    LOG_MSG_DEBUG(true, "started: sink_idx=%d  mac=%s  server=%s:%u",
        m_sink_idx, m_mac, m_ip, m_port);
}

void ModuleUdpLog::_stop()
{
    if (!m_sink_active) return;

    pal_logger_unregister_sink(m_sink_idx);
    pal_udp_log_stop();

    m_sink_active = false;
    m_sink_idx    = -1;

    LOG_MSG_DEBUG(true, "stopped");
}
