
#include "Arduino.h"
#include "board_mega.h"

void board_init(void){
  pinMode(c_pin_sdata1, OUTPUT);
  pinMode(c_pin_sdata2, OUTPUT);
  pinMode(c_pin_sclk1, OUTPUT);
  pinMode(c_pin_sclk2, OUTPUT);

  pinMode(c_pin_cs, OUTPUT);
  pinMode(c_pin_rclk, OUTPUT);

  pinMode(c_pin_red_led, OUTPUT);
  pinMode(c_pin_green_led, OUTPUT);
  pinMode(c_pin_nozzle1_io_port, OUTPUT);
  pinMode(c_pin_nozzle2_io_port, OUTPUT);

  pinMode(c_pin_encoder_btn, INPUT_PULLUP);
  pinMode(c_pin_btn_nzl_status, INPUT_PULLUP);
  pinMode(c_pin_btn_pumping, INPUT_PULLUP);
  pinMode(c_pin_button, INPUT_PULLUP);
  pinMode(c_pin_button_Tester, INPUT_PULLUP);

  digitalWrite(c_pin_sdata1, LOW); 
  digitalWrite(c_pin_sdata2, LOW);
  digitalWrite(c_pin_sclk1, HIGH); 
  digitalWrite(c_pin_sclk2, HIGH); 
  digitalWrite(c_pin_cs, HIGH);

  digitalWrite(c_pin_red_led, LOW);
  digitalWrite(c_pin_green_led, LOW); 


  Serial.begin(115200); 
}