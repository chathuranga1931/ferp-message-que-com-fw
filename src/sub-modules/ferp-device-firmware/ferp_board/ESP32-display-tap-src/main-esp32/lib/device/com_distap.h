#ifndef _COM_DISTAP_H_
#define _COM_DISTAP_H_

#include <stdbool.h>
#include "esp_err.h"
#include "display_types.h"

#define MAX_DATA 128

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    TX_ID_DEV_INFO = 0,
    TX_ID_SET_DISPLAY,
    TX_ID_SET_ERR_MASK,
    TX_ID_INPUTS,
    TX_ID_SIZE
} tx_pckt_id_t;

typedef enum
{
    RX_ID_DIS1_DATA = 0,
    RX_ID_DIS2_DATA,
    RX_ID_DIS_DATA_SIZE,
    RX_ID_DEV_INFO = RX_ID_DIS_DATA_SIZE,
    RX_ID_SET_DISPLAY,
    RX_ID_SET_ERR_MASK,
    RX_ID_INPUTS,
    RX_ID_KEEP_ALIVE,
    RX_ID_LOG_PRINTS,
    RX_ID_NACK,
    RX_ID_RAW_DIS1_DATA,   // raw capture chunk, channel 1 (types >= DIS_RAW_TYPE_BASE only)
    RX_ID_RAW_DIS2_DATA,   // raw capture chunk, channel 2 (types >= DIS_RAW_TYPE_BASE only)
    RX_ID_SIZE
} rx_pckt_id_t;

typedef union __attribute__((packed))
{
    struct
    {
        uint8_t pck_id;  // enum rx_pckt_id_t or tx_pckt_id_t
        uint8_t display; // enum display_type_t
        uint8_t length;
        uint8_t ab_data[];
    };
    uint8_t ab_raw[MAX_DATA + 2 + 3]; // 64 data bytes MAX + 2 CRC bytes + (pck_id+display+length)
} data_packet_t;

// RX_ID_RAW_DIS1_DATA / RX_ID_RAW_DIS2_DATA
// One capture batch (256 raw bytes per channel) arrives as chunk_count
// packets of chunk_len bytes each; log each chunk as it arrives rather
// than reassembling the batch first. Must match distap-esp32's device.h
// exactly.
typedef struct __attribute__((packed))
{
    uint8_t  codeword_bits; // 8 or 12 — self-describing capture width
    uint16_t total_len;     // total bytes in this capture batch (256)
    uint8_t  chunk_index;   // 0..(chunk_count-1)
    uint8_t  chunk_count;   // number of chunks per batch (4)
    uint8_t  chunk_len;     // bytes in this chunk (64)
    uint8_t  data[];        // chunk_len raw bytes
} raw_capture_chunk_t;

/**
 * Initialise sample
 *
 * @param
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t init_comms_distap(void (*dis1_fuel_event)(display_type_t type, uint8_t *data), void (*dis2_fuel_event)(display_type_t type, uint8_t *data),
                             void (*dis1_raw_event)(const raw_capture_chunk_t *chunk), void (*dis2_raw_event)(const raw_capture_chunk_t *chunk));

void suspend_comms_distap();

void resume_comms_distap();

bool distap_send_cmd(data_packet_t *rx, data_packet_t *tx, uint32_t tout);

#ifdef __cplusplus
}
#endif

#endif // _COM_DISTAP_H_