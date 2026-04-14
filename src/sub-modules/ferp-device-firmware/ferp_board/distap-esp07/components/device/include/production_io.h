#ifndef _PRODUCTION_IO_H_
#define _PRODUCTION_IO_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint16_t d1_sclk : 1;
    uint16_t d1_rclk : 1;
    uint16_t d1_sdata1 : 1;
    uint16_t d1_sdata2 : 1;

    uint16_t d2_sclk : 1;
    uint16_t d2_rclk : 1;
    uint16_t d2_sdata1 : 1;
    uint16_t d2_sdata2 : 1;

    uint16_t dis_enb : 1;

    uint16_t rest : 7;
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
esp_err_t production_io_init(xQueueHandle *send_que);

void dis_enb_output_set(bool level);

uint32_t dis_inputs_get(void);

#ifdef __cplusplus
}
#endif

#endif // _PRODUCTION_IO_H_