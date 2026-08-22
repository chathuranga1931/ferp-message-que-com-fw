#ifndef _DISPLAY_TYPES_H_
#define _DISPLAY_TYPES_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    DIS_NONE = 0,
    DIS_CENSTAR_6_DIGIT,        //1
    DIS_CENSTAR_7_DIGIT,        //2
    DIS_CENSTAR_7_DIGIT_CS,     //3
    DIS_HONGYANG_8_DIGIT,       //4
    DIS_WAYNE_6_DIGIT,          //5
    DIS_SANKI_6_DIGIT,          //6
    DIS_LONGFENG_8_DIGIT,       //7
    DIS_SIZE,

    // Raw capture types for reverse-engineering unknown pumps (not real
    // pumps — the DT board only captures + forwards raw codewords, no
    // decoding; main only logs them). Explicit values kept as a separate
    // numeric range above DIS_SIZE, listed after it here, so DIS_SIZE
    // itself stays 8 and DIS_SIZE-sized tables (e.g. pump_drivers[] in
    // module_fuel) never need to grow to accommodate them. Must be real
    // enumerators (not #defines) so any switch(display_type_t) doesn't
    // trip -Werror=switch, and must match distap-esp32's device.h exactly.
    DIS_RAW_8BIT_V1 = 90,  // 8-bit-per-codeword capture
    DIS_RAW_12BIT_V1 = 91, // 12-bit-per-codeword capture
}display_type_t; // 5-bit number, can add upto 31 display types

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

typedef union
{
    struct
    {
        uint32_t d1_sclk : 1;
        uint32_t d1_rclk : 1;
        uint32_t d1_sdata1 : 1;
        uint32_t d1_sdata2 : 1;

        uint32_t d2_sclk : 1;
        uint32_t d2_rclk : 1;
        uint32_t d2_sdata1 : 1;
        uint32_t d2_sdata2 : 1;

        uint32_t d1_in_cs : 1;
        uint32_t d2_in_cs : 1;

        uint32_t : 22;
    };
    uint32_t u32int;
} input_pin_t;

#ifdef __cplusplus
}
#endif

#endif // _DISPLAY_TYPES_H_