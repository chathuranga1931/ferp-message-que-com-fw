#include <string.h>
#include "pal_logger.h"
#include "pal/pal_i2c.h"
#include "ds1307.hpp"

#define __TAG__  "DS1307  "

// DS1307 I2C address
#define DS1307_I2C_ADDR     0x68

// DS1307 Register addresses
#define DS1307_REG_SECONDS  0x00
#define DS1307_REG_CONTROL  0x07

// I2C timeout in milliseconds
#define DS1307_I2C_TIMEOUT_MS  1000

// BCD conversion macros
#define BCD2DEC(val) (((val) / 16 * 10) + ((val) % 16))
#define DEC2BCD(val) ((((val) / 10) * 16) + ((val) % 10))

// Convert BCD to decimal
static uint8_t bcd_to_dec(uint8_t val) {
    return (val / 16 * 10) + (val % 16);
}

// Convert decimal to BCD
static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) * 16) + (val % 10);
}

int32_t ds1307_init(ds1307_init_t * init, ds1307_handle_t * handle){
    int32_t ret = ERROR_DS1307_OK;

    LOG_MSG_DEBUG(LOG_EN, "DS1307: Initializing...");

    // Read seconds register to check if clock is running
    uint8_t reg_addr = DS1307_REG_SECONDS;
    uint8_t seconds = 0;
    ret = pal_i2c_write_read(PAL_I2C_PORT_0, DS1307_I2C_ADDR, &reg_addr, 1, &seconds, 1, DS1307_I2C_TIMEOUT_MS);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "DS1307: Failed to read from device");
        return ERROR_DS1307_INIT_FAILED;
    }

    // Check if clock is halted (bit 7 of seconds register)
    if (seconds & 0x80) {
        LOG_MSG_DEBUG(LOG_EN, "DS1307: Clock is halted, enabling...");
        
        // Clear the CH (Clock Halt) bit to start the clock
        seconds &= 0x7F;
        uint8_t data[2] = {DS1307_REG_SECONDS, seconds};
        ret = pal_i2c_write(PAL_I2C_PORT_0, DS1307_I2C_ADDR, data, 2, DS1307_I2C_TIMEOUT_MS);
        
        if (ret != PAL_OK) {
            LOG_MSG_ERROR(LOG_EN, "DS1307: Failed to start the clock");
            return ERROR_DS1307_INIT_FAILED;
        }
        
        LOG_MSG_DEBUG(LOG_EN, "DS1307: Clock started successfully");
    } else {
        LOG_MSG_DEBUG(LOG_EN, "DS1307: Clock is already running");
    }

    LOG_MSG_DEBUG(LOG_EN, "DS1307: Initialized successfully");
    return ERROR_DS1307_OK;
}

int32_t ds1307_read_time(time_t * time_rtc){
    int32_t ret = ERROR_DS1307_OK;
    
    do {
        // Read 7 bytes from DS1307 (seconds, minutes, hours, day, date, month, year)
        uint8_t reg_addr = DS1307_REG_SECONDS;
        uint8_t data[7];
        ret = pal_i2c_write_read(PAL_I2C_PORT_0, DS1307_I2C_ADDR, &reg_addr, 1, data, 7, DS1307_I2C_TIMEOUT_MS);
        
        if (ret != PAL_OK) {
            LOG_MSG_ERROR(LOG_EN, "TS: Read date/time from RTC failed");
            ret = ERROR_DS1307_READ_TIME_FAILED;
            break;
        }

        // Convert BCD to decimal
        struct tm timeinfo = {0};
        timeinfo.tm_sec  = bcd_to_dec(data[0] & 0x7F);  // Mask CH bit
        timeinfo.tm_min  = bcd_to_dec(data[1]);
        timeinfo.tm_hour = bcd_to_dec(data[2] & 0x3F);  // Mask 12/24 hour bit
        timeinfo.tm_wday = bcd_to_dec(data[3]) - 1;     // Day of week (1-7) -> (0-6)
        timeinfo.tm_mday = bcd_to_dec(data[4]);
        timeinfo.tm_mon  = bcd_to_dec(data[5]) - 1;     // Month (1-12) -> (0-11)
        timeinfo.tm_year = bcd_to_dec(data[6]) + 100;   // Year since 1900 (2000-2099)

        // Convert to time_t
        time_t time_value = mktime(&timeinfo);
        
        // Check for invalid time (2000-01-01T00:00:00Z = 946684800)
        if (time_value == 946684800) {
            LOG_MSG_ERROR(LOG_EN, "TS: RTC is not set, or battery is dead");
            ret = ERROR_DS1307_BATTERY_DEAD;
            break;
        }
        
        if (time_value == (time_t)-1) {
            LOG_MSG_ERROR(LOG_EN, "TS: Invalid time from RTC");
            ret = ERROR_DS1307_READ_TIME_FAILED;
            break;
        }

        *time_rtc = time_value;
        ret = ERROR_DS1307_OK;
        
    } while(false);
    
    return ret;
}

int32_t ds1307_set_time(time_t time_rtc){
    int32_t ret = ERROR_DS1307_OK;
    
    do {
        // Convert time_t to struct tm
        struct tm *timeinfo = localtime(&time_rtc);
        if (timeinfo == NULL) {
            LOG_MSG_ERROR(LOG_EN, "TS: Invalid time value");
            ret = ERROR_DS1307_READ_TIME_FAILED;
            break;
        }

        // Convert decimal to BCD and prepare data
        uint8_t data[8];
        data[0] = DS1307_REG_SECONDS;                           // Register address
        data[1] = dec_to_bcd(timeinfo->tm_sec);                 // Seconds (0-59)
        data[2] = dec_to_bcd(timeinfo->tm_min);                 // Minutes (0-59)
        data[3] = dec_to_bcd(timeinfo->tm_hour);                // Hours (0-23)
        data[4] = dec_to_bcd(timeinfo->tm_wday + 1);            // Day of week (1-7)
        data[5] = dec_to_bcd(timeinfo->tm_mday);                // Date (1-31)
        data[6] = dec_to_bcd(timeinfo->tm_mon + 1);             // Month (1-12)
        data[7] = dec_to_bcd(timeinfo->tm_year % 100);          // Year (0-99)

        // Write all registers at once
        ret = pal_i2c_write(PAL_I2C_PORT_0, DS1307_I2C_ADDR, data, 8, DS1307_I2C_TIMEOUT_MS);
        
        if (ret != PAL_OK) {
            LOG_MSG_ERROR(LOG_EN, "TS: Set date/time to RTC failed");
            ret = ERROR_DS1307_READ_TIME_FAILED;
            break;
        }
        
        LOG_MSG_DEBUG(LOG_EN, "TS: Set date/time to RTC successfully");
        ret = ERROR_DS1307_OK;
        
    } while(false);
    
    return ret;
}

bool ds1307_is_running(void) {
    // Read seconds register
    uint8_t reg_addr = DS1307_REG_SECONDS;
    uint8_t seconds = 0;
    int32_t ret = pal_i2c_write_read(PAL_I2C_PORT_0, DS1307_I2C_ADDR, &reg_addr, 1, &seconds, 1, DS1307_I2C_TIMEOUT_MS);
    
    if (ret != PAL_OK) {
        return false;
    }
    
    // Check if CH bit (bit 7) is clear (clock running)
    return ((seconds & 0x80) == 0);
}
