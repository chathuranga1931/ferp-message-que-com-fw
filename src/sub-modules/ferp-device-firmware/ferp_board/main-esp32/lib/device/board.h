#ifndef _BOARD_H_
#define _BOARD_H_

// Board support package — common API.
//
// Every board has its own header (pin map, board-only API) and its own source
// file (board_init and the low-level functions).  The product selects the
// board with one BOARD_xxxx define and compiles only that board's .c file:
//
//   BOARD_2308  -> board_2308.h / board_2308.c   (ferp-com v1, ESP07 DT)
//   BOARD_2404  -> board_2404.h / board_2404.c   (ferp-com v2, ESP07 DT)
//   BOARD_2602  -> board_2602.h / board_2602.c   (ferp-com v3, ESP32 DT)

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#if defined(BOARD_2308)
    #include "board_2308.h"
#elif defined(BOARD_2404)
    #include "board_2404.h"
#elif defined(BOARD_2602)
    #include "board_2602.h"
#else
    #error "Define Board First"
#endif

// RTC EEPROM ADDRESSES
#define EEPROM_ADD_BOARD 0
#define EEPROM_ADD_DEVICE sizeof(board_meta_data_t)  //offset by board meta data size

typedef struct
{
    uint32_t board_type;
} board_meta_data_t;

typedef struct
{
    char uuid[10];
} device_meta_data_t;

#ifdef __cplusplus
extern "C"
{
#endif


/**
 * Initialise the board GPIOs and UART2 (DT link).
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t board_init();

bool gpio_get_input1();
bool gpio_get_input2();
bool gpio_get_input3();
bool gpio_get_input4();
bool gpio_get_input5();

void gpio_set_output1(const bool level);
void gpio_set_output2(const bool level);
void gpio_set_output3(const bool level);
void gpio_set_output4(const bool level);
void gpio_set_output5(const bool level);
void gpio_set_output6(const bool level);
void gpio_set_en4g(const bool level);

/** DT chip reset: true = hold in reset, false = run (polarity is board specific). */
void gpio_set_reset_distap(const bool level);

void gpio_set_io0_distap(const bool level);
void gpio_set_mode_output_io0_distap();
void gpio_reset_io0_distap();
void board_delay_ms(uint32_t ms);


#ifdef __cplusplus
}
#endif

#endif // _BOARD_H_
