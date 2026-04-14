#ifndef _FILE_DEVICE_H_
#define _FILE_DEVICE_H_

#include <stdbool.h>
#include "esp_err.h"

// uncomment to work with next version 1.2.x or higher
// #define DEV_VER_2

#define MAX_DATA 64

// Display packet sending times
#define DIFF_PCKT_SEND_MS 50  // if packet is different from previous, send after this time from last packet
#define SAME_PCKT_SEND_MS 1000 // if packet is same as previous, send after this time from last packet
// Allowable price gap
#define PRICE_GAP_LKR 10 // allowable price gap to not to mark any error

typedef enum
{
    RX_ID_DEV_INFO = 0,
    RX_ID_SET_DISPLAY,
#ifdef DEV_VER_2
    RX_ID_SET_ERR_MASK,
#endif
    RX_ID_INPUTS,
    RX_ID_SIZE
} rx_pckt_id_t;

typedef enum
{
    TX_ID_DIS1_DATA = 0,
    TX_ID_DIS2_DATA,
    TX_ID_DIS_DATA_SIZE,
    TX_ID_DEV_INFO = TX_ID_DIS_DATA_SIZE,
    TX_ID_SET_DISPLAY,
#ifdef DEV_VER_2
    TX_ID_SET_ERR_MASK,
#endif
    TX_ID_INPUTS,
    TX_ID_KEEP_ALIVE,
    TX_ID_NACK,
    TX_ID_SIZE
} tx_pckt_id_t;

typedef enum
{
    DIS_NONE = 0,
    DIS_CENSTAR_6_DIGIT,
    DIS_CENSTAR_7_DIGIT,
    DIS_CENSTAR_8_DIGIT,
    DIS_HONGYANG_8_DIGIT,
    DIS_WAYNE_6_DIGIT,
    DIS_SANKI_6_DIGIT,
    DIS_LONGFENG_8_DIGIT,
    DIS_SIZE
} display_type_t;

typedef union
{
    struct
    {
        uint8_t index : 1;     // mark error if index is not matching
        uint8_t unitprice : 1; // mark error if unit price indexes are not matching
        uint8_t totprice : 1;  // mark error if total price indexes are not matching
        uint8_t volume : 1;    // mark error if volume indexes are not matching
        uint8_t price_gap : 1; // mark error if price volume has a gap
        uint8_t : 3;
    } err_bit;
    uint8_t u8int;
}data_error_t;

typedef union
{
    struct
    {
    #ifdef DEV_VER_2
        uint8_t pck_id;  // enum rx_pckt_id_t or tx_pckt_id_t
        uint8_t display; // enum display_type_t
    #else
        struct
        {
            uint8_t pck_id : 3;  // enum rx_pckt_id_t or tx_pckt_id_t
            uint8_t display : 5; // enum display_type_t
        };
    #endif
        uint8_t length;
        uint8_t ab_data[MAX_DATA + 2]; // 64 data bytes MAX + 2 CRC bytes (CRC byte immediately follow data)
    };
    uint8_t ab_raw[MAX_DATA + 4];
} data_packet_t;

// ID RX_ID_DEV_INFO
typedef enum
{
    ID_CMD_DEV_VERSION = 0,
    ID_CMD_DEV_PROJ_NAME,
    ID_CMD_DEV_TIMEDATE,
    ID_CMD_DEV_SIZE
} id_dev_cmd_t;
typedef struct
{
    uint8_t command;
} rx_id_dev_info_t;
typedef struct
{
    uint8_t command;
    char version[32];
} tx_id_dev_version_t;
typedef struct
{
    uint8_t command;
    char prj_name[32];
} tx_id_dev_prj_name_t;
typedef struct
{
    uint8_t command;
    char time[16];
    char date[16];
} tx_id_dev_timedate_t;

// RX_ID_SET_DISPLAY
typedef enum
{
    ID_CMD_DIS_ALREADY = 0x00,
    ID_CMD_DIS_CHANGED = 0xFF
} id_cmd_dis_t;
typedef struct
{
    uint8_t display;
} rx_id_set_display_t;
typedef struct
{
    uint8_t state; // send 0xFF if changes, 0x00 already set the display
    uint8_t display;
} tx_id_set_display_t;

// RX_ID_SET_ERR_MASK
typedef enum
{
    ID_CMD_ERR_MASK_ALREADY = 0x00,
    ID_CMD_ERR_MASK_CHANGED = 0xFF
} id_cmd_err_mask_t;
typedef struct
{
    uint8_t err_mask;
} rx_id_set_err_mask_t;
typedef struct
{
    uint8_t state; // send 0xFF if changes, 0x00 already set the display
    uint8_t err_mask;
} tx_id_set_err_mask_t;

// ID RX_ID_INPUTS
typedef enum
{
    ID_CMD_INPUTS_PINS = 0, // reading inputs wihout  changing display enb/dis
    ID_CMD_INPUTS_ENABLE,
    ID_CMD_INPUTS_DISABLE,
    ID_CMD_INPUTS_SIZE
} id_inputs_cmd_t;
typedef struct
{
    uint8_t command;
} rx_id_inputs_t;
typedef struct
{
    uint32_t inputs;
} tx_id_inputs_t;

// TX_ID_KEEP_ALIVE
typedef struct
{
    uint32_t time_ms;
} tx_id_alive_t;

// ID_TX_ID_NACK
typedef enum
{
    ID_NACK_INVALID = 0,
    ID_NACK_OVERSIZE,
    ID_NACK_SIZE
} id_nack_reason_t;
typedef struct
{
    uint8_t reason;
} tx_id_nack_t;

/**
 * Initialise sample
 *
 * @param
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t device_init();

#endif // _FILE_DEVICE_H_
