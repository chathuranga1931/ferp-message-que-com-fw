
#ifndef _DIS_COMMON_H_
#define _DIS_COMMON_H_

#include "stdint.h"

// data capture defines
// #define CAPTURE_OLD

// enable to capture second screen
#define CAPTURE_DIS2

#ifdef CAPTURE_OLD

// data capture defines
#define DIS1_IN_RCLK 21
#define DIS1_OUT_CS 22 // chip select output, controlled by software, routed to CS Pin 23
#define DIS1_IN_CS 23     // chip select input
#define DIS1_IN_SCLK 18   // GPIO connected to SPI clock
#define DIS1_IN_DATA1 19  // GPIO connected to first data line
// hongyang data output pins
#define DIS1_OUT_RCLK 25
#define DIS1_OUT_SCLK 26
#define DIS1_OUT_DATA1 27
#define DIS1_OUT_DATA2 14

#else

//hardware SPI oprate pins
#define DIS1_IN_CS      27   // chip select input
#define DIS1_OUT_CS     14  // chip select output, controlled by software, routed to CS Pin 27
#define DIS2_IN_CS      12
#define DIS2_OUT_CS     13

//display capture pins
#define DIS1_IN_RCLK    26  //26 - sclk
#define DIS1_IN_SCLK    25  //25
#define DIS1_IN_DATA1   33  //33
#define DIS1_IN_DATA2   32

#define DIS2_IN_RCLK    35
#define DIS2_IN_SCLK    34
#define DIS2_IN_DATA1   39
#define DIS2_IN_DATA2   36

// hongyang data output pins
#define DIS1_OUT_RCLK   23
#define DIS1_OUT_SCLK   22
#define DIS1_OUT_DATA1  21
#define DIS1_OUT_DATA2  19
#define DIS2_OUT_RCLK   18
#define DIS2_OUT_SCLK   17
#define DIS2_OUT_DATA1  16
#define DIS2_OUT_DATA2  04

#endif

//get second display capture
#if (defined(CAPTURE_DIS2) && !defined(CAPTURE_OLD)) 
    #define DIS2_CAPTURE_ENABLE 1
#endif

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

typedef struct //__attribute__((packed))
{
    struct 
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t rest : 4;
	}flags;
    data_error_t error;
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
} dis_capture_t;

#endif /*_DIS_COMMON_H_*/