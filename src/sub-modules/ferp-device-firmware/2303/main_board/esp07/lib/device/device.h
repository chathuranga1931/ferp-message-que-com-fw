#ifndef _FILE_DEVICE_H_
#define _FILE_DEVICE_H_

#include "Arduino.h"

typedef enum
{
    DIS_CENSTAR_6_DIGIT = 0,
    DIS_CENSTAR_7_DIGIT,
    DIS_CENSTAR_8_DIGIT,
    DIS_CHINESE_8_DIGIT,
    DIS_WAYNE_6_DIGIT,
    DIS_SIZE
}display_type_t;

typedef union __attribute__((packed))
{
	struct
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t err_index : 1;				// mark error if index is not matching
		uint8_t err_unitprice : 1; 			// mark error if unit price indexes are not matching
		uint8_t err_totprice : 1;			// mark error if total price indexes are not matching
		uint8_t err_volume : 1;				// mark error if volume indexes are not matching
		uint8_t err_gap : 1;				// mark error if price volume has a gap
	};
	uint8_t byte;
} flags_t;

typedef struct __attribute__((packed))
{
	flags_t flags;
	uint32_t unit_price;  // 1 * 0.01 price
	uint32_t total_price; // 1 * 0.01 price
	uint32_t volume_l;	  // 1 * 0.001 volume
} display_data_t;

typedef struct __attribute__((packed))
{
	uint8_t sw_major;
	uint8_t sw_minor;
	uint8_t display_id;
	display_data_t display_data;
	uint32_t checksum;
} packet_data_t;

typedef struct
{
    display_type_t dis;
    // void (*send_dummy)(void);
    // bool (*send_display_data)(void);
    bool got_display_1;
    packet_data_t display_1;
    bool got_display_2;
    packet_data_t display_2;
    packet_data_t dummy;
} display_t;

/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
// void send_data_display(packet_data_t *pckt);
void send_serial(display_t *dis);
void recieve_serial(display_t *dis);
// void send_packet(packet_data_t *pckt);

#endif // _FILE_DEVICE_H_



