
#pragma once

#include "Arduino.h"

const char c_pin_sclk1=2;
const char c_pin_sclk=2; //for censtar

const char c_pin_sdata1=3;
const char c_pin_sdata2=5;

const char c_pin_sclk2=4;
const char c_pin_cs=6;
const char c_pin_rclk=6; //for censtar

const char c_pin_red_led=48;
const char c_pin_green_led=46;

const char c_pin_btn_nzl_status=52;
const char c_pin_button_Tester=52; //for censtar

const char c_pin_btn_pumping=50;
const char c_pin_button=50; //for censtar

const char c_pin_nozzle1_io_port=10;  // remapped from 11 (pin 11/PB5 hardware fault on board)
const char c_pin_nozzle2_io_port=12;

const char c_pin_encoder_btn=17;


// // Set pins 2, 3, 4, 5, and 6 LOW
// PORTE &= ~(1 << PE4); // Pin 2 LOW
// PORTE &= ~(1 << PE5); // Pin 3 LOW
// PORTG &= ~(1 << PG5); // Pin 4 LOW
// PORTE &= ~(1 << PE3); // Pin 5 LOW
// PORTH &= ~(1 << PH3); // Pin 6 LOW

// // Set pins 2, 3, 4, 5, and 6 HIGH
// PORTE |= (1 << PE4); // Pin 2 HIGH
// PORTE |= (1 << PE5); // Pin 3 HIGH
// PORTG |= (1 << PG5); // Pin 4 HIGH
// PORTE |= (1 << PE3); // Pin 5 HIGH
// PORTH |= (1 << PH3); // Pin 6 HIGH


#define CLOCK_1_HIGH    PORTE |= (1 << PE4) // Pin 2 HIGH
#define CLOCK_1_LOW     PORTE &= ~(1 << PE4) // Pin 2 LOW

#define CLOCK_2_HIGH    PORTG |= (1 << PG5) // Pin 4 HIGH
#define CLOCK_2_LOW     PORTG &= ~(1 << PG5) // Pin 4 LOW

#define DATA_1_WRITE(x) (x==1) ? PORTE |= (1 << PE5) : PORTE &= ~(1 << PE5) // Pin 3
#define DATA_2_WRITE(x) (x==1) ? PORTE |= (1 << PE3) : PORTE &= ~(1 << PE3) // Pin 5
#define CS_1_WRITE(x)  (x==1) ? PORTH |= (1 << PH3) : PORTH &= ~(1 << PH3) // Pin 6

void board_init(void);