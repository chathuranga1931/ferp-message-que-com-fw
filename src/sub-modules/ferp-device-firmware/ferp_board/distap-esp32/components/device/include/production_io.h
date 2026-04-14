#ifndef _PRODUCTION_IO_H_
#define _PRODUCTION_IO_H_

#include "stdbool.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

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


/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t production_io_init();

void dis_enb_output_set(bool level);
void dis_led_output_set(bool level);
void dis_out_cs1_set(bool level);
void dis_out_cs2_set(bool level);

uint32_t dis_inputs_get(void);

#ifdef __cplusplus
}
#endif

#endif // _PRODUCTION_IO_H_