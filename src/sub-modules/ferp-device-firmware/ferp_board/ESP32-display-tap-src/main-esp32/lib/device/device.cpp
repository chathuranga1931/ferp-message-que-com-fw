#include <Arduino.h>
#include "device.h"
#include "com_distap.h"
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
    return;
    const display_data_t *dis = (const display_data_t *)data;
    switch (type)
    {
    case DIS_HONGYANG_8_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
        {
            const int64_t gap = (((uint64_t)dis->unit_price * (uint64_t)dis->volume_l) / 1000) - (uint64_t)dis->total_price;
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0") + "  price gap = " + String(gap));
        }
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT_CS:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 7 cs display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 cs display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 100.0, 2) + ", " + "p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("sanki 6 display = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("sanki 6 display = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_LONGFENG_8_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("longfeng 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
        {
            const int64_t gap = (((uint64_t)dis->unit_price * (uint64_t)dis->volume_l) / 1000) - (uint64_t)dis->total_price;
            Serial.println("longfeng 8 display 1 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0") + "  price gap = " + String(gap));
        }
    }
    break;
    default:
        Serial.println("Display:" + String(type));
        break;
    }
}
void fuel_event_display_2(display_type_t type, uint8_t *data)
{
    return;
    const display_data_t *dis = (const display_data_t *)data;
    switch (type)
    {
    case DIS_HONGYANG_8_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT_CS:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("censtar 7 cs display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("censtar 7 cs display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0, 1) + ", " + String(dis->total_price / 10.0, 1) + ", " + String(dis->volume_l / 100.0, 2) + ", " + "p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0"));
    }
    break;
    case DIS_LONGFENG_8_DIGIT:
    {
        if (dis->flags.bits.select_ll)
            Serial.println("longfeng 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", Total Liters = " + String(dis->total_liters / 1000.0, 3));
        else
        {
            const int64_t gap = (((uint64_t)dis->unit_price * (uint64_t)dis->volume_l) / 1000) - (uint64_t)dis->total_price;
            Serial.println("longfeng 8 display 2 = " + String(dis->unit_price / 100.0, 2) + ", " + String(dis->total_price / 100.0, 2) + ", " + String(dis->volume_l / 1000.0, 3) + ", " + (dis->flags.bits.start_stop ? "start" : "stop") + ", p=" + (dis->flags.bits.select_p ? "1" : "0") + " l=" + (dis->flags.bits.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.bits.price_gap ? "1" : "0") + "  price gap = " + String(gap));
        }
    }
    break;
    default:
        Serial.println("Display:" + String(type));
        break;
    }
}

// Raw-capture callbacks (display types >= DIS_RAW_TYPE_BASE) — bench-test
// only, plain Serial hex dump so raw capture output can be verified
// directly on the bench without the full cloud-connected production app.
void raw_event_display_1(const raw_capture_chunk_t *chunk)
{
    String hex;
    for (uint8_t i = 0; i < chunk->chunk_len; i++)
    {
        if (chunk->data[i] < 0x10) hex += "0";
        hex += String(chunk->data[i], HEX) + " ";
    }
    Serial.println("RAW dis1 bits=" + String(chunk->codeword_bits) +
                    " chunk=" + String(chunk->chunk_index + 1) + "/" + String(chunk->chunk_count) +
                    " total=" + String(chunk->total_len) + "B: " + hex);
}
void raw_event_display_2(const raw_capture_chunk_t *chunk)
{
    String hex;
    for (uint8_t i = 0; i < chunk->chunk_len; i++)
    {
        if (chunk->data[i] < 0x10) hex += "0";
        hex += String(chunk->data[i], HEX) + " ";
    }
    Serial.println("RAW dis2 bits=" + String(chunk->codeword_bits) +
                    " chunk=" + String(chunk->chunk_index + 1) + "/" + String(chunk->chunk_count) +
                    " total=" + String(chunk->total_len) + "B: " + hex);
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

    //initilise communication with distap
    init_comms_distap(fuel_event_display_1, fuel_event_display_2, raw_event_display_1, raw_event_display_2);

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

    //keep distap running
    gpio_set_reset_distap(false);
    delay(500); // wait until distap wakeup complete after reset
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

