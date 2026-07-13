#ifndef _FILE_DEVICE_H_
#define _FILE_DEVICE_H_

#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

#define MAX_DATA 128

// Display packet sending times
#define DIFF_PCKT_SEND_MS 50  // if packet is different from previous, send after this time from last packet
#define SAME_PCKT_SEND_MS 1000 // if packet is same as previous, send after this time from last packet
// Allowable price gap
#define PRICE_GAP_LKR 10 // allowable price gap to not to mark any error

#define LOG_PRINT(FMRT, ARGS ...) { char str[MAX_DATA] = {}; const size_t str_size = sprintf(str, FMRT, ##ARGS); device_print_log(str, str_size);}

typedef enum
{
    RX_ID_DEV_INFO = 0,
    RX_ID_SET_DISPLAY,
    RX_ID_SET_ERR_MASK,
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
    TX_ID_SET_ERR_MASK,
    TX_ID_INPUTS,
    TX_ID_KEEP_ALIVE,
    TX_ID_LOG_PRINTS,
    TX_ID_NACK,
    // Raw capture chunks (types >= DIS_RAW_TYPE_BASE only) — one command
    // per physical channel PER data line, so all four streams are fully
    // independent at the protocol level (no shared field needed to tell
    // them apart).
    TX_ID_RAW_DIS1_L1_DATA, // channel 1, SDATA1
    TX_ID_RAW_DIS1_L2_DATA, // channel 1, SDATA2
    TX_ID_RAW_DIS2_L1_DATA, // channel 2, SDATA1
    TX_ID_RAW_DIS2_L2_DATA, // channel 2, SDATA2
    TX_ID_SIZE
} tx_pckt_id_t;

typedef enum
{
    DIS_NONE = 0,
    DIS_CENSTAR_6_DIGIT,
    DIS_CENSTAR_7_DIGIT,
    DIS_CENSTAR_7CS_DIGIT,
    DIS_HONGYANG_8_DIGIT,
    DIS_WAYNE_6_DIGIT,
    DIS_SANKI_6_DIGIT,
    DIS_LONGFENG_8_DIGIT,
    DIS_SIZE,

    // Raw capture types for reverse-engineering unknown pumps (not real
    // pumps — the DT board only captures + forwards raw codewords, no
    // decoding; main only logs them). Explicit values kept as a separate
    // numeric range above DIS_SIZE, listed after it here, so DIS_SIZE
    // itself stays 8 and DIS_SIZE-sized tables (e.g. pump_drivers[] on
    // main) never need to grow to accommodate them. Must be real
    // enumerators (not #defines) so switch(settings.display) — which is
    // display_type_t-typed — doesn't trip -Werror=switch.
    DIS_RAW_8BIT_V1 = 90,  // 8-bit-per-codeword capture
    DIS_RAW_12BIT_V1 = 91, // 12-bit-per-codeword capture
} display_type_t;

#define DIS_RAW_TYPE_BASE 90

static inline bool is_raw_capture_type(uint8_t display)
{
    return display >= DIS_RAW_TYPE_BASE;
}

typedef union
{
	struct 
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t rest : 4;
	} bits;
    uint8_t u8int;
}data_flags_t;

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
    } bits;
    uint8_t u8int;
}data_error_t;

typedef struct __attribute__((packed))
{
	data_flags_t flags; //fuel event flags
	data_error_t error; //fuel packet errors
	
    union
    {
        struct
        {
            uint32_t total_price; // 1 * 0.01 price
			uint32_t volume_l;	  // 1 * 0.001 volume
        };
        uint64_t total_liters; // 1 * 0.001
    };
    uint32_t unit_price;  // 1 * 0.01 price
} display_data_t;

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
    ID_CMD_INPUTS_DIS_ON,
    ID_CMD_INPUTS_DIS_OFF,
    ID_CMD_INPUTS_LED_ON,
    ID_CMD_INPUTS_LED_OFF,
    ID_CMD_INPUTS_CS1_ON,
    ID_CMD_INPUTS_CS1_OFF,
    ID_CMD_INPUTS_CS2_ON,
    ID_CMD_INPUTS_CS2_OFF,
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

// TX_ID_RAW_DIS1_L1_DATA / _L2 / TX_ID_RAW_DIS2_L1_DATA / _L2
// SDATA1 and SDATA2 share one SCLK/RCLK per physical channel but carry
// independent data, so each is captured and sent under its own pck_id —
// no shared "which line" field needed in the payload, the command itself
// says which channel+line this batch is from. One capture batch (128 raw
// bytes per channel per line) is sent as chunk_count packets of chunk_len
// bytes each; the receiver logs each chunk as it arrives rather than
// reassembling the batch first.
typedef struct __attribute__((packed))
{
    uint8_t  codeword_bits; // 8 or 12 — self-describing capture width
    uint16_t total_len;     // total bytes in this capture batch (128)
    uint8_t  chunk_index;   // 0..(chunk_count-1)
    uint8_t  chunk_count;   // number of chunks per batch (4)
    uint8_t  chunk_len;     // bytes in this chunk (32)
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
esp_err_t device_init();

void device_print_log(const char *str, size_t len);

#endif // _FILE_DEVICE_H_
