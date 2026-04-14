#include <Arduino.h>
#include <SPIFFS.h>
#include "usb_printer.h"

#define CMD_INIT 0x1b, 0x40
#define CMD_META 0x1b, 0x52, 0x00, 0x1b, 0x63, 0x33, 0x04, 0x0d
#define CMD_CHAR_NORMAL 0x1D, 0x21, 0x00
#define CMD_FONT_A 0x1b, 0x21, 0x00
#define CMD_FONT_B 0x1b, 0x21, 0x01 
// #define CMD_ 
// #define CMD_ 
// #define CMD_ 
// #define CMD_ 
// #define CMD_ 


const uint8_t print_cmd[] = {
  CMD_INIT,
  CMD_META,
  CMD_CHAR_NORMAL,
  CMD_FONT_B,
  'H','e','l','l','o',' ','W','o','r','l','d',' ','!','!','!','\r','\n'
};

void setup() {
  Serial.begin(115200);
  Serial.println("\r\n\r\n");
  Serial.println("Starting Main Board...!");
  SPIFFS.begin();
  usb_printer_init();
}

void loop() {
  //waiting for printer connection
  while (usb_printer_connected() == false)
  {
    delay(1);
  }
  //send print command
  usb_printer_send_data(print_cmd, sizeof(print_cmd));

  //wait for disconnect
  while (usb_printer_connected() == true)
  {
    delay(1);
  }

  // delay(1000);
}
