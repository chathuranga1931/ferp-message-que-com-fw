/**
 * @file pal_mac_wifi.cpp
 * @brief PAL WiFi implementation for macOS simulator.
 *
 * The Mac simulator has no real WiFi hardware.  This stub:
 *   - Stores the STA config and callback supplied by pal_wifi_init().
 *   - On pal_wifi_sta_connect() fires a background thread that emits:
 *       STA_CONNECTED  → short delay → GOT_IP
 *     simulating a real association + DHCP sequence.
 *   - IP, MAC, SSID and RSSI are read from the HOST system's en0 interface
 *     so the simulator reflects real network state.
 *
 * The callback fires on a background thread; ModuleWifi must not call
 * pal_wifi_ functions from within the callback (keep them side-effect free).
 */

#include "pal_wifi.h"
#include "pal_logger.h"

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __TAG__    "PAL_WIFI"
#define WIFI_LOG   true

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------

static pal_wifi_init_config_t    s_cfg{};
static pal_wifi_event_callback_t s_cb       = nullptr;
static void                     *s_user_data = nullptr;
static volatile bool             s_connected = false;
static char                      s_ip[PAL_WIFI_IP_STR_LEN]  = "0.0.0.0";
static char                      s_mac[PAL_WIFI_MAC_STR_LEN] = "00:00:00:00:00:00";

// ---------------------------------------------------------------------------
// Helper — run a shell command and capture first line of stdout
// Returns true if command succeeded and output is non-empty.
// ---------------------------------------------------------------------------

static bool _shell_query(const char *cmd, char *out, size_t max_len)
{
    if (!cmd || !out || max_len == 0) return false;
    out[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return false;
    bool ok = (fgets(out, (int)max_len, fp) != nullptr);
    pclose(fp);
    // Strip trailing newline/whitespace
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' '))
        out[--len] = '\0';
    return ok && (len > 0);
}

// ---------------------------------------------------------------------------
// Read real system WiFi info from en0 into module-private state
// ---------------------------------------------------------------------------

static void _refresh_system_wifi_info(void)
{
    // IP address: ifconfig en0 → first inet line → second field
    _shell_query(
        "ifconfig en0 2>/dev/null | grep 'inet ' | awk '{print $2}' | head -1",
        s_ip, sizeof(s_ip));
    if (s_ip[0] == '\0') strncpy(s_ip, "0.0.0.0", sizeof(s_ip) - 1);

    // MAC address: ifconfig en0 → ether line → second field, upper-cased
    char mac_raw[PAL_WIFI_MAC_STR_LEN] = {};
    _shell_query(
        "ifconfig en0 2>/dev/null | grep 'ether ' | awk '{print $2}' | head -1",
        mac_raw, sizeof(mac_raw));
    if (mac_raw[0] != '\0') {
        // Convert to upper-case for consistency
        for (size_t i = 0; mac_raw[i]; i++)
            s_mac[i] = (char)((mac_raw[i] >= 'a' && mac_raw[i] <= 'f')
                               ? (mac_raw[i] - 32) : mac_raw[i]);
        s_mac[strlen(mac_raw)] = '\0';
    } else {
        strncpy(s_mac, "00:00:00:00:00:00", sizeof(s_mac) - 1);
    }

    LOG_MSG_INFO(WIFI_LOG, "system wifi: ip=%s  mac=%s", s_ip, s_mac);
}

// ---------------------------------------------------------------------------
// Background connect thread
// ---------------------------------------------------------------------------

static void *_connect_thread(void * /*arg*/)
{
    // Simulate association delay (~0.5 s)
    usleep(500 * 1000);

    if (s_cb) s_cb(PAL_WIFI_EVENT_STA_CONNECTED, nullptr, s_user_data);

    LOG_MSG_INFO(WIFI_LOG, "STA_CONNECTED (sim)");

    // Simulate DHCP delay (~0.5 s), then read real system IP/MAC
    usleep(500 * 1000);

    _refresh_system_wifi_info();

    s_connected = true;
    if (s_cb) s_cb(PAL_WIFI_EVENT_STA_GOT_IP, nullptr, s_user_data);

    LOG_MSG_INFO(WIFI_LOG, "STA_GOT_IP ip=%s (sim)", s_ip);
    return nullptr;
}

// ---------------------------------------------------------------------------
// PAL interface
// ---------------------------------------------------------------------------

int32_t pal_wifi_init(const pal_wifi_init_config_t *config,
                      pal_wifi_event_callback_t     event_callback,
                      void                         *user_data)
{
    if (!config) return -1;
    memcpy(&s_cfg, config, sizeof(s_cfg));
    s_cb        = event_callback;
    s_user_data = user_data;
    LOG_MSG_INFO(WIFI_LOG, "init ssid=\"%s\" (sim)", s_cfg.config.sta.ssid);
    return 0;
}

int32_t pal_wifi_deinit(void)
{
    s_cb = nullptr;
    s_connected = false;
    return 0;
}

int32_t pal_wifi_start(void)
{
    LOG_MSG_INFO(WIFI_LOG, "start (sim)");
    return 0;
}

int32_t pal_wifi_stop(void)
{
    s_connected = false;
    if (s_cb) s_cb(PAL_WIFI_EVENT_STA_DISCONNECTED, nullptr, s_user_data);
    return 0;
}

int32_t pal_wifi_sta_connect(void)
{
    LOG_MSG_INFO(WIFI_LOG, "sta_connect → spawning sim thread");
    pthread_t tid;
    pthread_create(&tid, nullptr, _connect_thread, nullptr);
    pthread_detach(tid);
    return 0;
}

int32_t pal_wifi_sta_disconnect(void)
{
    s_connected = false;
    if (s_cb) s_cb(PAL_WIFI_EVENT_STA_DISCONNECTED, nullptr, s_user_data);
    return 0;
}

bool pal_wifi_sta_is_connected(void)
{
    return s_connected;
}

int32_t pal_wifi_sta_get_rssi(int8_t *rssi)
{
    if (!rssi) return -1;
    // Try to read real RSSI from airport utility
    char buf[16] = {};
    bool ok = _shell_query(
        "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/"
        "Current/Resources/airport -I 2>/dev/null | grep ' agrCtlRSSI' | awk '{print $2}'",
        buf, sizeof(buf));
    if (ok && buf[0] != '\0') {
        *rssi = (int8_t)atoi(buf);
    } else {
        *rssi = -55;   // fallback
    }
    return 0;
}

int32_t pal_wifi_ap_get_sta_count(uint8_t *num_sta)
{
    if (!num_sta) return -1;
    *num_sta = 0;
    return 0;
}

int32_t pal_wifi_get_mac(uint8_t mac[6])
{
    if (!mac) return -1;
    // Return bytes from s_mac string "AA:BB:CC:DD:EE:FF"
    sscanf(s_mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    return 0;
}

int32_t pal_wifi_get_mac_str(char *mac_str, size_t max_len)
{
    if (!mac_str || max_len < PAL_WIFI_MAC_STR_LEN) return -1;
    strncpy(mac_str, s_mac, max_len - 1);
    mac_str[max_len - 1] = '\0';
    return 0;
}

int32_t pal_wifi_get_ip_str(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < PAL_WIFI_IP_STR_LEN) return -1;
    strncpy(ip_str, s_ip, max_len - 1);
    ip_str[max_len - 1] = '\0';
    return 0;
}

int32_t pal_wifi_get_status(pal_wifi_status_t *status)
{
    if (!status) return -1;
    int8_t rssi = -55;
    pal_wifi_sta_get_rssi(&rssi);
    status->is_connected = s_connected;
    status->rssi         = rssi;
    status->channel      = 6;
    strncpy(status->ip_addr,  s_ip,  sizeof(status->ip_addr)  - 1);
    strncpy(status->mac_addr, s_mac, sizeof(status->mac_addr) - 1);
    return 0;
}

int32_t pal_wifi_get_mode(pal_wifi_mode_t *mode)
{
    if (!mode) return -1;
    *mode = PAL_WIFI_MODE_STA;
    return 0;
}

uint8_t pal_wifi_rssi_to_level(int8_t rssi, uint8_t num_levels)
{
    if (num_levels == 0) return 0;
    // Map -100 dBm .. 0 dBm → 0 .. num_levels-1
    int clamped = rssi < -100 ? -100 : (rssi > 0 ? 0 : (int)rssi);
    int normalized = clamped + 100;   // 0..100
    return (uint8_t)(normalized * (num_levels - 1) / 100);
}
