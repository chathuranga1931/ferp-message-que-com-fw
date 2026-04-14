#ifndef _COM_ESP07_H_
#define _COM_ESP07_H_

#include <stdbool.h>
#include "esp_err.h"
#include "display_types.h"

#define MAX_DATA 64

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    TX_ID_DEV_INFO = 0,
    TX_ID_SET_DISPLAY,
    TX_ID_INPUTS,
    TX_ID_SIZE
} tx_pckt_id_t;

typedef enum
{
    RX_ID_DIS1_DATA = 0,
    RX_ID_DIS2_DATA,
    RX_ID_DEV_INFO,
    RX_ID_SET_DISPLAY,
    RX_ID_INPUTS,
    RX_ID_KEEP_ALIVE,
    RX_ID_NACK,
    RX_ID_SIZE
} rx_pckt_id_t;

typedef union
{
    struct
    {
        struct
        {
            uint8_t pck_id : 3; //enum rx_pckt_id_t or tx_pckt_id_t
            uint8_t display : 5; //enum display_type_t
        };
        uint8_t length;
        uint8_t ab_data[MAX_DATA + 2]; // 64 data bytes MAX + 2 CRC bytes (CRC byte immediately follow data)
    };
    uint8_t ab_raw[MAX_DATA + 4];
} data_packet_t;

/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t init_comms_esp07(void (*dis1_fuel_event)(display_type_t type, uint8_t *data), void (*dis2_fuel_event)(display_type_t type, uint8_t *data));

void suspend_comms_esp07();

void resume_comms_esp07();

bool esp07_send_cmd(data_packet_t *rx, data_packet_t *tx, uint32_t tout);

#ifdef __cplusplus
}
#endif

#endif // _COM_ESP07_H_