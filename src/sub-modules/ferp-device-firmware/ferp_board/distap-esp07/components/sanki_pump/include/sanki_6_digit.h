#ifndef _FILE_SANKI_6_DIGIT_H_
#define _FILE_SANKI_6_DIGIT_H_

#include "esp_err.h"
#include "freertos/queue.h"
#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t display_sanki_6_digit_init(xQueueHandle *send_queue);

#ifdef __cplusplus
}
#endif

#endif // _FILE_SANKI_6_DIGIT_H_