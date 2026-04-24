// cube_sphere_cloud_driver.cpp
//
// Implements cloud_driver_t by mapping the generic cloud_driver types to the
// cube_sphere C API.

#include "cube_sphere_cloud_driver.h"
#include "cube_sphere_api.h"
#include "cloud_driver.h"

#include <string.h>

// ── register_device ───────────────────────────────────────────────────────────

static int32_t _register_device(const char *mac_address_str, const char *root_ca)
{
    return cube_sphere_register((char *)mac_address_str, root_ca);
}

// ── get_nozzle_config ─────────────────────────────────────────────────────────

static int32_t _get_nozzle_config(cloud_nozzle_config_t *out, uint8_t count)
{
    nozzel_config_t tmp[NO_NOZZELS] = {};
    int32_t ret = cube_sphere_get_nozzle_config(tmp);
    uint8_t n = (count < NO_NOZZELS) ? count : NO_NOZZELS;
    for (uint8_t i = 0; i < n; i++) {
        strncpy(out[i].nozzle_uuid,   tmp[i].uuid,           sizeof(out[i].nozzle_uuid)   - 1);
        strncpy(out[i].nozzle_id,     tmp[i].nozzle_id,      sizeof(out[i].nozzle_id)     - 1);
        strncpy(out[i].fuel_type,     tmp[i].fuel_type,      sizeof(out[i].fuel_type)     - 1);
        strncpy(out[i].fuel_type_str, tmp[i].fuel_type_str,  sizeof(out[i].fuel_type_str) - 1);
    }
    return ret;
}

// ── send_startup / send_status_updated (shared builder) ──────────────────────

static startup_info_t _to_startup_info(cloud_startup_info_t info)
{
    startup_info_t s = {};
    strncpy(s.ssid,             info.ssid,             sizeof(s.ssid)             - 1);
    strncpy(s.password,         info.password,         sizeof(s.password)         - 1);
    strncpy(s.ip_address,       info.ip_address,       sizeof(s.ip_address)       - 1);
    strncpy(s.mac_address_str,  info.mac_address_str,  sizeof(s.mac_address_str)  - 1);
    strncpy(s.fw_version,       info.fw_version,       sizeof(s.fw_version)       - 1);
    strncpy(s.hw_version,       info.hw_version,       sizeof(s.hw_version)       - 1);
    strncpy(s.board_version,    info.board_version,    sizeof(s.board_version)    - 1);
    strncpy(s.device_type,      info.device_type,      sizeof(s.device_type)      - 1);
    strncpy(s.esp07_fw_version, info.esp07_fw_version, sizeof(s.esp07_fw_version) - 1);
    strncpy(s.sd_card_status,   info.sd_card_status,   sizeof(s.sd_card_status)   - 1);
    strncpy(s.sd_card_size_str, info.sd_card_size_str, sizeof(s.sd_card_size_str) - 1);
    s.rssi                        = info.rssi;
    s.uptime_sec                  = info.uptime_sec;
    s.nozzle_event_count_success  = info.event_count_success;
    s.nozzle_event_count_failure  = info.event_count_failure;
    return s;
}

static int32_t _send_startup(cloud_startup_info_t info)
{
    return cube_sphere_send_startup(_to_startup_info(info));
}

static int32_t _send_status_updated(cloud_startup_info_t info)
{
    return cube_sphere_send_status_updated(_to_startup_info(info));
}

// ── send_heartbeat ────────────────────────────────────────────────────────────

static int32_t _send_heartbeat(cloud_hb_info_t info)
{
    heart_beat_info_t hb = {
        .rssi                       = info.rssi,
        .uptime_sec                 = info.uptime_sec,
        .nozzle_event_count_success = info.event_count_success,
        .nozzle_event_count_failure = info.event_count_failure,
    };
    return cube_sphere_send_hb(hb);
}

// ── send_reconnect ────────────────────────────────────────────────────────────

static int32_t _send_reconnect(cloud_reconnect_info_t info)
{
    reconnect_info_t r = {};
    strncpy(r.ssid,       info.ssid,       sizeof(r.ssid)       - 1);
    strncpy(r.password,   info.password,   sizeof(r.password)   - 1);
    strncpy(r.ip_address, info.ip_address, sizeof(r.ip_address) - 1);
    r.rssi       = info.rssi;
    r.uptime_sec = info.uptime_sec;
    return cube_sphere_send_reconnect(r);
}

// ── send_pumped ───────────────────────────────────────────────────────────────

static int32_t _send_pumped(cloud_pumped_info_t event)
{
    pumped_event_info_t pe = {
        .n_idx           = event.nozzle_idx,
        .time_stamp      = event.time_stamp,
        .unit_pricex100  = event.unit_pricex100,
        .total_pricex100 = event.total_pricex100,
        .volume_lx1000   = event.volume_lx1000,
        .event_id        = event.event_id,
    };
    return cube_sphere_send_pumped(pe);
}

// ── send_printed ──────────────────────────────────────────────────────────────

static int32_t _send_printed(cloud_pumped_info_t event)
{
    pumped_event_info_t pe = {
        .n_idx           = event.nozzle_idx,
        .time_stamp      = event.time_stamp,
        .unit_pricex100  = event.unit_pricex100,
        .total_pricex100 = event.total_pricex100,
        .volume_lx1000   = event.volume_lx1000,
        .event_id        = event.event_id,
    };
    return cube_sphere_send_printed(pe);
}

// ── get_device_uuid ───────────────────────────────────────────────────────────

static int32_t _get_device_uuid(char *out, uint32_t max_len)
{
    return cube_sphere_get_device_uuid(out, max_len);
}

// ── Driver singleton ──────────────────────────────────────────────────────────

static const cloud_driver_t k_cube_sphere_driver = {
    .register_device    = _register_device,
    .get_nozzle_config  = _get_nozzle_config,
    .send_startup       = _send_startup,
    .send_heartbeat     = _send_heartbeat,
    .send_reconnect     = _send_reconnect,
    .send_pumped        = _send_pumped,
    .send_printed       = _send_printed,
    .send_status_updated = _send_status_updated,
    .get_device_uuid    = _get_device_uuid,
};

const cloud_driver_t *cloud_driver_cube_sphere(void)
{
    return &k_cube_sphere_driver;
}
