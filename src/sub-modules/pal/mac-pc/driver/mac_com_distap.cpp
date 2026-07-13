/**
 * @file mac_com_distap.cpp
 * @brief Simulator implementation of com_distap.h (com_distap.c on ESP-IDF).
 *
 * In the simulator build there is no UART / DT board.  Instead:
 *
 *   init_comms_distap()          — stores the four frame/raw callbacks, no hardware init
 *   mac_distap_inject_frame()    — called from mac_driver when the Python UI
 *                                  sends a SIM_DISTAP_FRAME JSON command over TCP
 *   mac_distap_inject_raw_chunk()— same idea, for the raw-capture path
 *                                  (display types >= DIS_RAW_TYPE_BASE)
 *   suspend_comms_distap()       — no-op
 *   resume_comms_distap()        — no-op
 *   distap_send_cmd()            — no-op stub (returns true)
 */

#include "com_distap.h"
#include "pal_logger.h"
#include <string.h>
#include <stddef.h>

#define __TAG__       "MAC_DIS "
#define MAC_DIS_LOG   true

#define MLOG(fmt, ...)  LOG_MSG_INFO( MAC_DIS_LOG, fmt, ##__VA_ARGS__)
#define MLOGE(fmt, ...) LOG_MSG_ERROR(MAC_DIS_LOG, fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Stored callbacks
// ---------------------------------------------------------------------------

static void (*s_dis1_cb)(display_type_t, uint8_t *) = nullptr;
static void (*s_dis2_cb)(display_type_t, uint8_t *) = nullptr;
static void (*s_dis1_raw_cb)(const raw_capture_chunk_t *) = nullptr;
static void (*s_dis2_raw_cb)(const raw_capture_chunk_t *) = nullptr;

// ---------------------------------------------------------------------------
// com_distap.h API — simulator implementations
// ---------------------------------------------------------------------------

esp_err_t init_comms_distap(
    void (*dis1_fuel_event)(display_type_t type, uint8_t *data),
    void (*dis2_fuel_event)(display_type_t type, uint8_t *data),
    void (*dis1_raw_event)(const raw_capture_chunk_t *chunk),
    void (*dis2_raw_event)(const raw_capture_chunk_t *chunk))
{
    s_dis1_cb = dis1_fuel_event;
    s_dis2_cb = dis2_fuel_event;
    s_dis1_raw_cb = dis1_raw_event;
    s_dis2_raw_cb = dis2_raw_event;
    MLOG("init_comms_distap — callbacks stored (no UART in simulator)");
    return ESP_OK;
}

void suspend_comms_distap()
{
    MLOG("suspend_comms_distap — no-op");
}

void resume_comms_distap()
{
    MLOG("resume_comms_distap — no-op");
}

bool distap_send_cmd(data_packet_t * /*rx*/, data_packet_t * /*tx*/, uint32_t /*tout*/)
{
    // All command/response exchanges are handled by mac_cmd_distap.cpp stubs.
    return true;
}

// ---------------------------------------------------------------------------
// Simulator injection API (called from mac_driver.cpp)
// ---------------------------------------------------------------------------

extern "C" void mac_distap_inject_frame(uint8_t   nozzle_idx,
                                         int32_t   display_type_int,
                                         uint32_t  flags_raw,
                                         uint32_t  error_raw,
                                         uint32_t  unit_pricex100,
                                         uint32_t  total_pricex100,
                                         uint32_t  vol_lx1000)
{
    display_data_t data{};
    data.flags.u8int  = (uint8_t)flags_raw;
    data.error.u8int  = (uint8_t)error_raw;
    data.unit_price   = unit_pricex100;
    data.total_price  = total_pricex100;
    data.volume_l     = vol_lx1000;

    auto dtype = (display_type_t)display_type_int;

    if (nozzle_idx == 0 && s_dis1_cb) {
        s_dis1_cb(dtype, (uint8_t *)&data);
        // MLOG("Injected frame to DIS1: type=%d flags=0x%02X error=0x%02X unit_price=%u total_price=%u volume_l=%u",
        //      (int)dtype, data.flags.u8int, data.error.u8int, data.unit_price, data.total_price, data.volume_l);
    } else if (nozzle_idx == 1 && s_dis2_cb) {
        s_dis2_cb(dtype, (uint8_t *)&data);
        // MLOG("Injected frame to DIS2: type=%d flags=0x%02X error=0x%02X unit_price=%u total_price=%u volume_l=%u",
        //      (int)dtype, data.flags.u8int, data.error.u8int, data.unit_price, data.total_price, data.volume_l);
    } else {
        MLOGE("inject_frame: nozzle_idx=%u out of range or no callback", (unsigned)nozzle_idx);
    }
}

// Symmetric injection for the raw-capture path (display types >= 90) —
// lets the simulator UI feed synthetic raw chunks to exercise the
// logging-only consumer without real DT board hardware.
extern "C" void mac_distap_inject_raw_chunk(uint8_t        nozzle_idx,
                                             uint8_t        codeword_bits,
                                             uint16_t       total_len,
                                             uint8_t        chunk_index,
                                             uint8_t        chunk_count,
                                             uint8_t        chunk_len,
                                             const uint8_t *chunk_data)
{
    uint8_t buf[MAX_DATA] = {};
    raw_capture_chunk_t *chunk = (raw_capture_chunk_t *)buf;
    chunk->codeword_bits = codeword_bits;
    chunk->total_len     = total_len;
    chunk->chunk_index   = chunk_index;
    chunk->chunk_count   = chunk_count;
    chunk->chunk_len     = chunk_len;
    if (chunk_len > MAX_DATA - offsetof(raw_capture_chunk_t, data)) {
        MLOGE("inject_raw_chunk: chunk_len=%u too large", (unsigned)chunk_len);
        return;
    }
    memcpy(chunk->data, chunk_data, chunk_len);

    if (nozzle_idx == 0 && s_dis1_raw_cb) {
        s_dis1_raw_cb(chunk);
    } else if (nozzle_idx == 1 && s_dis2_raw_cb) {
        s_dis2_raw_cb(chunk);
    } else {
        MLOGE("inject_raw_chunk: nozzle_idx=%u out of range or no callback", (unsigned)nozzle_idx);
    }
}
