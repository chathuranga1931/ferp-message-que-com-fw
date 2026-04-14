
#include <Arduino.h>
#include "errno.h"
#include "board.h"

#define MAX_LED_PATTERN_LENGTH (20)

#define LED_CUE_RESOLUTION      (250)   //ms

#define LED_CUE_BM_RED      (0b001)
#define LED_CUE_BM_GREEN1   (0b010)
#define LED_CUE_BM_GREEN2   (0b100)

// typedef struct{
//     uint16_t repeat_count;
//     uint16_t led_pattern_length;
//     uint8_t led_pattern[MAX_LED_PATTERN_LENGTH];
// }led_cue_t;

// led_cue_t _led_cue;

uint16_t led_red_delay;
uint16_t led_green1_delay;
uint16_t led_green2_delay;

uint8_t led_red_status;
uint8_t led_green1_status;
uint8_t led_green2_status;

void led_init(){
    led_red_delay = 0;
    led_green1_delay = 0;
    led_green2_delay = 0;

    led_red_status = 0;
    led_green1_status = 0;
    led_green2_status = 0;
}

void led_red_blink_delay(uint16_t delay){
    led_red_delay = delay;
}

void led_green1_blink_delay(uint16_t delay){
    led_green1_delay = delay;
}

void led_green2_blink_delay(uint16_t delay){
    led_green2_delay = delay;
}

void led_process(){

    static unsigned long ts_red;
    static unsigned long ts_green1;
    static unsigned long ts_green2;

    if(led_red_delay == 0){
        digitalWrite(GPIO_LED_RED, LOW);
        led_red_status = 1;
    }
    else if(led_red_delay == 0xFFFF){
        digitalWrite(GPIO_LED_RED, HIGH);
    }
    else{
        if(millis() - ts_red > led_red_delay){
            digitalWrite(GPIO_LED_RED, led_red_status);
            led_red_status = led_red_status == 1 ? 0 : 1;
            ts_red = millis();
        }
    }

    if(led_green1_delay == 0){
        led_green1_status = 1;
        digitalWrite(GPIO_LED_GREEN1, LOW);
    }
    else if(led_green1_delay == 0xFFFF){
        digitalWrite(GPIO_LED_GREEN1, HIGH);
    }
    else{
        if(millis() - ts_green1 > led_green1_delay){
            digitalWrite(GPIO_LED_GREEN1, led_green1_status);
            led_green1_status = led_green1_status == 1 ? 0 : 1;
            ts_green1 = millis();
        }
    }

    if(led_green2_delay == 0){
        led_green2_status = 1;
        digitalWrite(GPIO_LED_GREEN2, LOW);
    }
    else if(led_green2_delay == 0xFFFF){
        digitalWrite(GPIO_LED_GREEN2, HIGH);
    }
    else{
        if(millis() - ts_green2 > led_green2_delay){
            digitalWrite(GPIO_LED_GREEN2, led_green2_status);
            led_green2_status = led_green2_status == 1 ? 0 : 1;
            ts_green2 = millis();
        }
    }
}