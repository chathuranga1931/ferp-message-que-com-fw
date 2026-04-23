
#ifndef _FERP_BSP_BOARD_H_
#define _FERP_BSP_BOARD_H_

#include <time.h>

/* GPIO Configs */

#if defined(FERP_COM_2308)
    #error "Including board configuration for COM 2308"
#include "com-boards/2308/board_2308_wrap.h"
#elif defined(FERP_COM_2404)
    // #error "Including board configuration for COM 2404"
    #include "com-boards/2404/board_2404_wrap.h"
#elif defined(FERP_COM_2602)
    #error "Including board configuration for COM 2602"
    #include "com-boards/2602/board_2602_wrap.h"
#endif

typedef void (*cb_button_events_t)(void * arg, uint64_t timestamp_us);
typedef struct {
    cb_button_events_t cb_event;
    void * arg;
}button_event_cb_info_t;

// Define an event type for button actions
typedef enum {
    DEFAULT_BUTTON_EVENT_PRESS,
    DEFAULT_BUTTON_EVENT_RELEASE,
    PRINT1_BUTTON_EVENT_PRESS,
    PRINT1_BUTTON_EVENT_RELEASE,
    PRINT2_BUTTON_EVENT_PRESS,
    PRINT2_BUTTON_EVENT_RELEASE,
    NOZZLE1_START,
    NOZZLE1_STOP,
    NOZZLE2_START,
    NOZZLE2_STOP,
} button_events_t;

// Define an event structure to include a timestamp
typedef struct {
    button_events_t event_type;
    uint64_t timestamp_us; // Timestamp in microseconds
} button_event_t;

void board_ini(void);
void board_process(void * arg);

bool is_i2c_device_connected(uint8_t address);

void board_register_cb_on_button_default_press(cb_button_events_t event, void * arg);
void board_register_cb_on_button_default_release(cb_button_events_t event,void * arg);
void board_register_cb_on_button_print1_press(cb_button_events_t event, void * arg);
void board_register_cb_on_button_print1_release(cb_button_events_t event, void * arg);
void board_register_cb_on_button_print2_press(cb_button_events_t event, void * arg);
void board_register_cb_on_button_print2_release(cb_button_events_t event, void * arg);

void board_register_cb_on_button_print2_release(cb_button_events_t event, void * arg);
void board_register_cb_on_button_nozzle1_start(cb_button_events_t event, void * arg);
void board_register_cb_on_button_nozzle1_stop(cb_button_events_t event, void * arg);
void board_register_cb_on_button_nozzle2_start(cb_button_events_t event, void * arg);
void board_register_cb_on_button_nozzle2_stop(cb_button_events_t event, void * arg);

void board_buz_on();
void board_buz_off();

void board_led1_on();
void board_led1_off();

void board_led2_on();
void board_led2_off();

void board_restart();
unsigned long board_millis();
void board_get_mac_address(uint8_t *mac_address, uint8_t mac_address_len);
void board_get_board_id(char * board_id_str, uint8_t * board_id, uint8_t size);

// SD Card SPI pins
uint8_t board_get_sd_card_ss_pin();
uint8_t board_get_sd_card_mosi_pin();
uint8_t board_get_sd_card_miso_pin();
uint8_t board_get_sd_card_sck_pin();

// Time management functions
int32_t board_set_timeofday(time_t epoch_time);
int32_t board_get_timeofday(time_t *epoch_time);
void board_get_date_string(char *buffer, size_t max_len);

// void board_get_board_id(std::string * board_id_str, uint8_t * board_id, uint8_t size);

#endif //_FERP_BSP_BOARD_H_