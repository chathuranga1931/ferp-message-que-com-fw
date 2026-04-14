
#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include "pal_preprocessor.h"
#include "board.h"
#include "pal_logger.h"
#include "pal_gpio.h"
#include "pal_i2c.h"
#include "pal_efuse.h"
#include "pal_time.h"
#include "hsys_queue.h"

#include "drivers/DS1307/ds1307.hpp"

#define __TAG__  "BOARD   "

// GPIO level constants
#define GPIO_LOW         0
#define GPIO_HIGH        1
#define GPIO_UNKNOWN    -1

static hsys_queue_handle_t button_event_queue;

void button_event_task(void* pvParameters);

static button_event_cb_info_t cb_default_button_release;
static button_event_cb_info_t cb_default_button_press;
static button_event_cb_info_t cb_print1_button_release;
static button_event_cb_info_t cb_print1_button_press;
static button_event_cb_info_t cb_print2_button_release;
static button_event_cb_info_t cb_print2_button_press;
static button_event_cb_info_t cb_nozzle1_start;
static button_event_cb_info_t cb_nozzle1_stop;
static button_event_cb_info_t cb_nozzle2_start;
static button_event_cb_info_t cb_nozzle2_stop;

// ISR-Safe function to get time in microseconds
uint32_t PAL_ON_IRAM_ATTR get_time_from_isr_us()
{
    return (uint32_t)pal_time_get_us_from_isr();
}

int PAL_ON_IRAM_ATTR process_button_event(
    int last_button_state, 
    int current_button_state, 
    button_events_t press_event, 
    button_events_t release_event){

    button_event_t event;
    event.timestamp_us = get_time_from_isr_us(); // Get the timestamp in microseconds

    if ((current_button_state == GPIO_LOW && last_button_state == GPIO_HIGH) 
    || (current_button_state == GPIO_LOW && last_button_state == GPIO_UNKNOWN))
    {
        // Falling edge detected
        event.event_type = release_event;
    } 
    else if ((current_button_state == GPIO_HIGH && last_button_state == GPIO_LOW) 
    || (current_button_state == GPIO_HIGH && last_button_state == GPIO_UNKNOWN))
    {
        // Rising edge detected
        event.event_type = press_event;
    } 
    else 
    {
        // No valid edge detected
        return last_button_state;
    }

    last_button_state = current_button_state; // Update last state

    // Send the event to the queue from ISR
    bool xHigherPriorityTaskWoken = false;
    hsys_queue_send_from_isr(&button_event_queue, &event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) 
    {
        hsys_yield_from_isr();
    }

    return last_button_state;
}

void PAL_ON_IRAM_ATTR button_default_isr_handler(int32_t gpio_num, void* arg) 
{
    static int last_button_state = GPIO_UNKNOWN;
    int current_button_state = pal_gpio_read(DEFAULT_BUTTON_GPIO_PIN);

    last_button_state = process_button_event(
        last_button_state, 
        current_button_state, 
        DEFAULT_BUTTON_EVENT_PRESS,
        DEFAULT_BUTTON_EVENT_RELEASE
    );
}

void PAL_ON_IRAM_ATTR button_print1_isr_handler(int32_t gpio_num, void* arg) 
{
    static int last_button_state = GPIO_UNKNOWN;
    int current_button_state = pal_gpio_read(PRINT1_BUTTON_GPIO_PIN);

    last_button_state = process_button_event(
        last_button_state, 
        current_button_state, 
        PRINT1_BUTTON_EVENT_PRESS,
        PRINT1_BUTTON_EVENT_RELEASE
        
    );
}

void PAL_ON_IRAM_ATTR button_print2_isr_handler(int32_t gpio_num, void* arg) {
    static int last_button_state = GPIO_UNKNOWN;
    int current_button_state = pal_gpio_read(PRINT2_BUTTON_GPIO_PIN);

    last_button_state = process_button_event(
        last_button_state, 
        current_button_state, 
        PRINT2_BUTTON_EVENT_PRESS,
        PRINT2_BUTTON_EVENT_RELEASE
    );    
}

void PAL_ON_IRAM_ATTR input_nozzle1_isr_handler(int32_t gpio_num, void* arg) 
{
    static int last_button_state = GPIO_UNKNOWN;
    int current_button_state = pal_gpio_read(NOZZLE1_GPIO_PIN);

    last_button_state = process_button_event(
        last_button_state, 
        current_button_state,
        NOZZLE1_STOP, 
        NOZZLE1_START 
    );
}

void PAL_ON_IRAM_ATTR input_nozzle2_isr_handler(int32_t gpio_num, void* arg) 
{
    static int last_button_state = GPIO_UNKNOWN;
    int current_button_state = pal_gpio_read(NOZZLE2_GPIO_PIN);

    last_button_state = process_button_event(
        last_button_state, 
        current_button_state, 
        NOZZLE2_STOP,
        NOZZLE2_START
    );
}

/**
 * @brief Initialize board
 *
 * This function initializes the board, including the I2C, inputs, outputs, LEDs, and UART2.
 *
 */
void board_ini()
{
    // Initialize I2C using PAL interface
    pal_i2c_config_t i2c_config = {
        .mode = PAL_I2C_MODE_MASTER,
        .sda_pin = I2C_SDA,
        .scl_pin = I2C_SCL,
        .sda_pullup_enable = true,
        .scl_pullup_enable = true,
        .clock_speed = 100000  // 100 kHz
    };
    pal_i2c_init(PAL_I2C_PORT_0, &i2c_config);

    // Initialize board-specific configurations
    board_init();
    
    // Create button event queue using hsys_queue
    hsys_queue_init(&button_event_queue, 10, sizeof(button_event_t));
    
    // Attach interrupts using PAL GPIO interface
    pal_gpio_set_interrupt(DEFAULT_BUTTON_GPIO_PIN, PAL_GPIO_INTR_ANYEDGE, button_default_isr_handler, NULL);
    pal_gpio_set_interrupt(PRINT1_BUTTON_GPIO_PIN, PAL_GPIO_INTR_ANYEDGE, button_print1_isr_handler, NULL);
    pal_gpio_set_interrupt(PRINT2_BUTTON_GPIO_PIN, PAL_GPIO_INTR_ANYEDGE, button_print2_isr_handler, NULL);
    pal_gpio_set_interrupt(NOZZLE1_GPIO_PIN, PAL_GPIO_INTR_ANYEDGE, input_nozzle1_isr_handler, NULL);
    pal_gpio_set_interrupt(NOZZLE2_GPIO_PIN, PAL_GPIO_INTR_ANYEDGE, input_nozzle2_isr_handler, NULL);
    
    // Initialize DS1307 RTC
    ds1307_init(NULL, NULL);
}

void board_get_mac_address(uint8_t *mac_address, uint8_t mac_address_len)
{
    if(mac_address == NULL)
    {
        return;
    }

    if(mac_address_len < 6)
    {
        return;
    }

    pal_efuse_get_mac(mac_address, mac_address_len);
}

unsigned long board_millis()
{
    // Use PAL time interface for platform independence
    return (unsigned long)pal_time_get_ms();
}


void board_get_board_id(char * board_id_str, uint8_t * board_id, uint8_t size)
{
    uint8_t board_id_tmp[8] = {0};
    size_t id_len = sizeof(board_id_tmp);
    pal_efuse_get_chip_id(board_id_tmp, &id_len);

    // Allocate a buffer for the ID string
    char id_str[25]; // Format is "XXXXXXXXXXXXXXXX" (16 hex chars + null terminator)
    // Format the chip ID
    snprintf(id_str, sizeof(id_str), "%02X%02X%02X%02X%02X%02X%02X%02X",
             board_id_tmp[0], board_id_tmp[1], board_id_tmp[2], board_id_tmp[3], 
             board_id_tmp[4], board_id_tmp[5], board_id_tmp[6], board_id_tmp[7]);

    if(size >= 8 && board_id != NULL)
    {
        for(int i = 0; i < 8; i++)
        {
            board_id[i] = board_id_tmp[i];
        }
    }

    if(board_id_str != NULL)
    {
        strcpy(board_id_str, id_str);
    }

    LOG_MSG_DEBUG(LOG_EN, "Board ID: %s", id_str);
}


/**
 * @brief Get the SD card SS pin.
 *
 * @return The SD card SS pin.
 */

uint8_t board_get_sd_card_ss_pin()
{
    return SPI_CS_SD;
}

uint8_t board_get_sd_card_mosi_pin()
{
    return SPI_MOSI;
}

uint8_t board_get_sd_card_miso_pin()
{
    return SPI_MISO;
}

uint8_t board_get_sd_card_sck_pin()
{
    return SPI_SCLK;
}

bool is_i2c_device_connected(uint8_t address)
{
    return pal_i2c_device_probe(PAL_I2C_PORT_0, address, 1000);
}

// Task to handle button events
void board_process(void *arg) 
{
    button_event_t event;
    if (hsys_queue_receive(&button_event_queue, &event, 0) == true) 
    {
        // LOG_MSG_DEBUG(LOG_EN, "Event Received @ %lld Event %d", event.timestamp_us, event.event_type);
        if (event.event_type == DEFAULT_BUTTON_EVENT_PRESS) 
        {
            if(cb_default_button_press.cb_event)
            {
                cb_default_button_press.cb_event(cb_default_button_press.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == DEFAULT_BUTTON_EVENT_RELEASE) 
        {
            if(cb_default_button_release.cb_event)
            {
                cb_default_button_release.cb_event(cb_default_button_release.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == PRINT1_BUTTON_EVENT_PRESS) 
        {
            if(cb_print1_button_press.cb_event)
            {
                cb_print1_button_press.cb_event(cb_print1_button_press.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == PRINT1_BUTTON_EVENT_RELEASE) 
        {
            if(cb_print1_button_release.cb_event)
            {
                cb_print1_button_release.cb_event(cb_print1_button_release.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == PRINT2_BUTTON_EVENT_PRESS) 
        {
            if(cb_print2_button_press.cb_event)
            {
                cb_print2_button_press.cb_event(cb_print2_button_press.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == PRINT2_BUTTON_EVENT_RELEASE) 
        {
            if(cb_print2_button_release.cb_event)
            {
                cb_print2_button_release.cb_event(cb_print2_button_release.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == NOZZLE1_START) 
        {
            if(cb_nozzle1_start.cb_event)
            {
                cb_nozzle1_start.cb_event(cb_nozzle1_start.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == NOZZLE1_STOP) 
        {
            if(cb_nozzle1_stop.cb_event)
            {
                cb_nozzle1_stop.cb_event(cb_nozzle1_stop.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == NOZZLE2_START) 
        {
            if(cb_nozzle2_start.cb_event)
            {
                cb_nozzle2_start.cb_event(cb_nozzle2_start.arg, event.timestamp_us);
            }
        } 
        else if (event.event_type == NOZZLE2_STOP) 
        {
            if(cb_nozzle2_stop.cb_event)
            {
                cb_nozzle2_stop.cb_event(cb_nozzle2_stop.arg, event.timestamp_us);
            }
        }
    }
}

void board_register_cb_on_button_default_press(cb_button_events_t event, void * arg) 
{
    cb_default_button_press.cb_event = event;
    cb_default_button_press.arg = arg;
}

void board_register_cb_on_button_default_release(cb_button_events_t event, void * arg) 
{
    cb_default_button_release.cb_event  = event;
    cb_default_button_release.arg = arg;
}

void board_register_cb_on_button_print1_press(cb_button_events_t event, void * arg) 
{
    cb_print1_button_press.cb_event = event;
    cb_print1_button_press.arg = arg;
}

void board_register_cb_on_button_print1_release(cb_button_events_t event, void * arg) 
{
    cb_print1_button_release.cb_event  = event;
    cb_print1_button_release.arg = arg;
}

void board_register_cb_on_button_print2_press(cb_button_events_t event, void * arg) 
{
    cb_print2_button_press.cb_event = event;
    cb_print2_button_press.arg = arg;
}

void board_register_cb_on_button_print2_release(cb_button_events_t event, void * arg) 
{
    cb_print2_button_release.cb_event  = event;
    cb_print2_button_release.arg = arg;
}

void board_register_cb_on_button_nozzle1_start(cb_button_events_t event, void * arg) 
{
    cb_nozzle1_start.cb_event  = event;
    cb_nozzle1_start.arg = arg;
}

void board_register_cb_on_button_nozzle1_stop(cb_button_events_t event, void * arg) 
{
    cb_nozzle1_stop.cb_event  = event;
    cb_nozzle1_stop.arg = arg;
}

void board_register_cb_on_button_nozzle2_start(cb_button_events_t event, void * arg) 
{
    cb_nozzle2_start.cb_event  = event;
    cb_nozzle2_start.arg = arg;
}

void board_register_cb_on_button_nozzle2_stop(cb_button_events_t event, void * arg) 
{
    cb_nozzle2_stop.cb_event  = event;
    cb_nozzle2_stop.arg = arg;
}

/**
 * @brief Set the system time from epoch time
 * 
 * @param epoch_time The time in seconds since Unix epoch (1970-01-01 00:00:00 UTC)
 * @return int32_t 0 on success, -1 on failure
 */
int32_t board_set_timeofday(time_t epoch_time) 
{
    struct timeval tv;
    tv.tv_sec = epoch_time;
    tv.tv_usec = 0;
    
    int ret = settimeofday(&tv, NULL);
    if (ret == 0) 
    {
        LOG_MSG_DEBUG(LOG_EN, "System time set to epoch: %ld", epoch_time);
        return 0;
    } 
    else 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to set system time");
        return -1;
    }
}

/**
 * @brief Get the current system time as epoch time
 * 
 * @param epoch_time Pointer to store the epoch time (seconds since 1970-01-01)
 * @return int32_t 0 on success, -1 on failure
 */
int32_t board_get_timeofday(time_t *epoch_time) 
{
    if (epoch_time == NULL) 
    {
        LOG_MSG_ERROR(LOG_EN, "epoch_time pointer is NULL");
        return -1;
    }
    
    struct timeval tv;
    int ret = gettimeofday(&tv, NULL);
    if (ret == 0) 
    {
        *epoch_time = tv.tv_sec;
        return 0;
    } 
    else 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to get system time");
        return -1;
    }
}

/**
 * @brief Get the current date as a formatted string (YYYYMMDD)
 * 
 * @param buffer Buffer to store the date string
 * @param max_len Maximum length of the buffer (should be at least 9 bytes)
 */
void board_get_date_string(char *buffer, size_t max_len) 
{
    if (buffer == NULL || max_len < 9) 
    {
        LOG_MSG_ERROR(LOG_EN, "Invalid buffer or size");
        return;
    }
    
    struct timeval tv;
    struct tm timeinfo;
    
    // Get current time
    if (gettimeofday(&tv, NULL) != 0) 
    {
        LOG_MSG_ERROR(LOG_EN, "Failed to get time");
        snprintf(buffer, max_len, "19700101"); // Default to epoch start
        return;
    }
    
    // Convert to local time
    localtime_r(&tv.tv_sec, &timeinfo);
    
    // Format as YYYYMMDD
    snprintf(buffer, max_len, "%04d%02d%02d",
             timeinfo.tm_year + 1900,  // tm_year is years since 1900
             timeinfo.tm_mon + 1,      // tm_mon is 0-11, so add 1
             timeinfo.tm_mday);        // tm_mday is 1-31
    
    LOG_MSG_DEBUG(LOG_EN, "Date string: %s", buffer);
}

void board_delay_ms(uint32_t ms)
{
    pal_time_delay_ms(ms);
}