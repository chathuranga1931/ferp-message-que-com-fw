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
    DIS_CENSTAR_6_DIGIT,
    DIS_CENSTAR_7_DIGIT,
    DIS_CENSTAR_8_DIGIT,
    DIS_HONGYANG_8_DIGIT,
    DIS_WAYNE_6_DIGIT,
    DIS_SANKI_6_DIGIT,
    DIS_SIZE
}display_type_t; // 5-bit number, can add upto 31 display types

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

typedef struct __attribute__((packed))
{
	// flags_t flags;
	struct 
	{
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t : 5;
	}flags;
	// struct
	// {
	// 	uint8_t index : 1;				// mark error if index is not matching
	// 	uint8_t unitprice : 1; 			// mark error if unit price indexes are not matching
	// 	uint8_t totprice : 1;			// mark error if total price indexes are not matching
	// 	uint8_t volume : 1;				// mark error if volume indexes are not matching
	// 	uint8_t price_gap : 1;		    // mark error if price volume has a gap
	// 	uint8_t : 3;
	// }error;
	data_error_t error; //fuel packet errors

    union
    {
        struct
        {
            uint32_t total_price; // 1 * 0.1 price
			uint32_t volume_l;	  // 1 * 0.001 volume
        };
        uint64_t total_liters; // 1 * 0.001
    };
    uint32_t unit_price;  // 1 * 0.1 price
} cens_6_digit_t;

typedef struct __attribute__((packed))
{
	struct 
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t rest : 4;
	}flags;
	// struct
	// {
	// 	uint8_t index : 1;				// mark error if index is not matching
	// 	uint8_t unitprice : 1; 			// mark error if unit price indexes are not matching
	// 	uint8_t totprice : 1;			// mark error if total price indexes are not matching
	// 	uint8_t volume : 1;				// mark error if volume indexes are not matching
	// 	uint8_t price_gap : 1;		    // mark error if price volume has a gap
	// 	uint8_t rest : 3;
	// }error;
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
} cens_7_digit_t;

typedef struct __attribute__((packed))
{
	struct 
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t rest : 4;
	}flags;
	// struct
	// {
	// 	uint8_t index : 1;				// mark error if index is not matching
	// 	uint8_t unitprice : 1; 			// mark error if unit price indexes are not matching
	// 	uint8_t totprice : 1;			// mark error if total price indexes are not matching
	// 	uint8_t volume : 1;				// mark error if volume indexes are not matching
	// 	uint8_t price_gap : 1;		    // mark error if price volume has a gap
	// 	uint8_t rest : 3;
	// }error;
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
} hya_8_digit_t;

typedef struct __attribute__((packed))
{
	// flags_t flags;
	struct 
	{
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t : 5;
	}flags;
	// struct
	// {
	// 	uint8_t price_gap : 1;		    // mark error if price volume has a gap
	// 	uint8_t : 7;
	// }error;
	data_error_t error; //fuel packet errors
	
    union
    {
        struct
        {
            uint32_t total_price; // 1 * 0.1 price
			uint32_t volume_l;	  // 1 * 0.01 volume
        };
        uint64_t total_liters; // 1 * 0.001
    };
    uint32_t unit_price;  // 1 * 0.1 price
} wyn_6_digit_t;

typedef struct __attribute__((packed))
{
	// flags_t flags;
	struct 
	{
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t select_ll : 1;
		uint8_t : 5;
	}flags;
	// union
	// {
	// 	struct
	// 	{
	// 		uint8_t index : 1;				// mark error if index is not matching
	// 		uint8_t unitprice : 1; 			// mark error if unit price indexes are not matching
	// 		uint8_t totprice : 1;			// mark error if total price indexes are not matching
	// 		uint8_t volume : 1;				// mark error if volume indexes are not matching
	// 		uint8_t price_gap : 1;		    // mark error if price volume has a gap
	// 		uint8_t : 3;
	// 	} error;
	// 	uint8_t errors;
	// };	
	data_error_t error; //fuel packet errors
    union
    {
        struct
        {
            uint32_t total_price; // 1 * 0.1 price
			uint32_t volume_l;	  // 1 * 0.001 volume
        };
        uint64_t total_liters; // 1 * 0.001
    };
    uint32_t unit_price;  // 1 * 0.1 price
} sanki_6_digit_t;

#ifdef __cplusplus
}
#endif

#endif // _DISPLAY_TYPES_H_