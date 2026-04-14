#include <Arduino.h>
#include "device.h"
#include "com_esp07.h"
#include <Wire.h>
#include <WiFi.h>
#include "DS1307RTC.h"

void (*pwr_dw_cb_evt)(void*) = NULL;
void *pwr_dw_arg = NULL;
time_t pwr_dw_time = 0;
esp_timer_handle_t power_down_timer = NULL;

const char *system_get_reset_reason()
{
    const char *resetreason[] = {
        "ESP_RST_UNKNOWN",
        "ESP_RST_POWERON",
        "ESP_RST_EXT",
        "ESP_RST_SW",
        "ESP_RST_PANIC",
        "ESP_RST_INT_WDT",
        "ESP_RST_TASK_WDT",
        "ESP_RST_WDT",
        "ESP_RST_DEEPSLEEP",
        "ESP_RST_BROWNOUT",
        "ESP_RST_SDIO"};

    esp_reset_reason_t rst = esp_reset_reason();
    if (rst <= ESP_RST_SDIO)
        return resetreason[rst];
    else
        return resetreason[0];
}

void board_evt_pwrdw_timer(void *arg)
{
    if(millis() - pwr_dw_time > 200) //if not called this for more than 200ms
    {
        if(pwr_dw_cb_evt)
            pwr_dw_cb_evt(pwr_dw_arg);
    }
    pwr_dw_time = millis(); //restart timer if continuously calling this callback
}

IRAM_ATTR void board_evt_pwrdw_iterrupt(void)
{
    esp_timer_start_once(power_down_timer, 1); // timeout for no time
}

void fuel_event_display_1(display_type_t type, uint8_t *data)
{
    switch (type)
    {
    case DIS_HONGYANG_8_DIGIT:
    {
        const hya_8_digit_t *dis = (hya_8_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
        {
            const int64_t gap = (((uint64_t)dis->unit_price * (uint64_t)dis->volume_l) / 1000) - (uint64_t)dis->total_price;
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0") + "  price gap = " + String(gap));
        }
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        const cens_6_digit_t *dis = (cens_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        const cens_7_digit_t *dis = (cens_7_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        const wyn_6_digit_t *dis = (wyn_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 100.0, 2) + ", " + "p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        const sanki_6_digit_t *dis = (sanki_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("sanki 6 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("sanki 6 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    default:
        Serial.println("Display:" + String(type));
        break;
    }
}
void fuel_event_display_2(display_type_t type, uint8_t *data)
{
    switch (type)
    {
    case DIS_HONGYANG_8_DIGIT:
    {
        const hya_8_digit_t *dis = (hya_8_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        const cens_6_digit_t *dis = (cens_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        const cens_7_digit_t *dis = (cens_7_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        const wyn_6_digit_t *dis = (wyn_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 100.0, 2) + ", " + "p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        const sanki_6_digit_t *dis = (sanki_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    default:
        Serial.println("Display:" + String(type));
        break;
    }
}

void initBoard(void (*pwr_dw_cb)(void*), void *arg)
{
    board_meta_data_t data = {};

    //initialise board pheripherals
    board_init();

    //begin I2C communication
    Wire.begin(I2C_SDA, I2C_SCL, 100000);
    //begin Real Time Clock
    RTC.begin(&Wire);

    //initilise communication with esp07
    init_comms_esp07(fuel_event_display_1, fuel_event_display_2);

    // //add power down call back
    // pwr_dw_cb_evt = pwr_dw_cb;
    // pwr_dw_arg = arg;
    // const esp_timer_create_args_t powerdoen_timer_args = {
    //     .callback = board_evt_pwrdw_timer,
    //     .arg = (void*)power_down_timer,
    //     .name = "power down"
    // };
    // attachInterrupt(VIN_LOW, board_evt_pwrdw_iterrupt, FALLING);
    // esp_timer_create(&powerdoen_timer_args, &power_down_timer);

    //get Reset reason
    Serial.println("\r\n\r\nBoard Reset reason\t:" + String(system_get_reset_reason()));
    //get RTC memory firmware information
    const bool ret = getBoardMetaData(&data);
    Serial.println("Board Version\t\t:" + String(data.board_type));
    //get ESP32 mac information
    Serial.println("Board MAC STA\t\t:" + WiFi.macAddress());

    //keep esp07 running
    gpio_set_reset_esp07(false);
    delay(500); // wait until esp07 wakeup complete after reset
}
bool getBoardMetaData(board_meta_data_t *data)
{
    return RTC.getEEEPROMdata(data, EEPROM_ADD_BOARD, sizeof(board_meta_data_t));
}
bool setBoardMetaData(board_meta_data_t *data)
{
    return RTC.setEEEPROMdata(data, EEPROM_ADD_BOARD, sizeof(board_meta_data_t));
}
bool getDeviceMetaData(device_meta_data_t *data)
{
    return RTC.getEEEPROMdata(data, EEPROM_ADD_DEVICE, sizeof(device_meta_data_t));
}
bool setDeviceMetaData(device_meta_data_t *data)
{
    return RTC.setEEEPROMdata(data, EEPROM_ADD_DEVICE, sizeof(device_meta_data_t));
}

