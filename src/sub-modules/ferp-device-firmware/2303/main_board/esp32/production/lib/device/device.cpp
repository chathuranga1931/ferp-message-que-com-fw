#include <Arduino.h>
#include "device.h"
#include "board_2303.h"
#include "com_esp07.h"

void fuel_event_display_1(display_type_t type, uint8_t *data)
{
    switch (type)
    {
    case DIS_HONGYANG_8_DIGIT:
    {
        const hya_8_digit_t *dis = (hya_8_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("chinese 8 display 1 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        const cens_6_digit_t *dis = (cens_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("censtar 6 display 1 = " + String(dis->unit_price / 10.0) + ", " + String(dis->total_price / 10.0) + ", " + String(dis->volume_l / 1000.0) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        const cens_7_digit_t *dis = (cens_7_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("censtar 7 display 1 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        const wyn_6_digit_t *dis = (wyn_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("wayne 6 display 1 = " + String(dis->unit_price / 10.0) + ", " + String(dis->total_price / 10.0) + ", " + String(dis->volume_l / 100.0) + ", " + "p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        const sanki_6_digit_t *dis = (sanki_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("sanki 6 display 1 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("sanki 6 display 1 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
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
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("chinese 8 display 2 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_6_DIGIT:
    {
        const cens_6_digit_t *dis = (cens_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("censtar 6 display 2 = " + String(dis->unit_price / 10.0) + ", " + String(dis->total_price / 10.0) + ", " + String(dis->volume_l / 1000.0) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_CENSTAR_7_DIGIT:
    {
        const cens_7_digit_t *dis = (cens_7_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("censtar 7 display 2 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + (dis->flags.start_stop ? "start" : "stop") + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_WAYNE_6_DIGIT:
    {
        const wyn_6_digit_t *dis = (wyn_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("wayne 6 display 2 = " + String(dis->unit_price / 10.0) + ", " + String(dis->total_price / 10.0) + ", " + String(dis->volume_l / 100.0) + ", " + "p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    case DIS_SANKI_6_DIGIT:
    {
        const sanki_6_digit_t *dis = (sanki_6_digit_t *)data;
        if (dis->flags.select_ll)
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0) + ", Total Liters = " + String(dis->total_liters / 1000.0));
        else
            Serial.println("sanki 6 display 2 = " + String(dis->unit_price / 100.0) + ", " + String(dis->total_price / 100.0) + ", " + String(dis->volume_l / 1000.0) + ", " + ", p=" + (dis->flags.select_p ? "1" : "0") + " l=" + (dis->flags.select_l ? "1" : "0") + " err_byte=" + String(*(uint8_t*)&dis->error) + " err_price=" + (dis->error.err_bit.price_gap ? "1" : "0"));
    }
    break;
    default:
        Serial.println("Display:" + String(type));
        break;
    }
}

void initBoard(void (*pwr_dw_cb)(void))
{
    // pwr_dw_cb_evt = pwr_dw_cb;
    init_board();
    init_comms_esp07(fuel_event_display_1, fuel_event_display_2);
    // attachInterrupt(VIN_LOW, pwr_dw_cb, ONLOW);
    gpio_set_reset_esp07(false);
    delay(500); // wait until esp07 wakeup complete after reset
}

void initDisplay()
{
    init_comms_esp07(fuel_event_display_1, fuel_event_display_2);
}