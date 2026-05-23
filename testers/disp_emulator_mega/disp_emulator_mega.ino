#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/wdt.h>

#include "Encoder.h"
#include "sanki-6.h"
#include "censtar-6.h"
#include "board_mega.h"

typedef enum {
    device_configuring,
    device_running,
} device_state_t;

typedef enum{
    display_type_sanki_6digit = 0,
    display_type_censtar_6digit,
    display_type_censtar_7digit,
    display_type_wayne_6digit,
    display_type_hongyang_8digit,
} display_type_t;

const int pump_type_count = 5;
const String pump_types[pump_type_count] = {
    "SANKI-6-DIGIT",
    "CENSTAR-6-DIGIT",
    "CENSTAR-7-DIGIT",
    "WAYNE-6-DIGIT",
    "HONGYANG-8-DIGIT",
};

display_type_t display_type = display_type_sanki_6digit;
device_state_t device_state = device_configuring;

int16_t display_mode_curser_x;
int16_t display_mode_curser_y;

const String displayMode = "SANKI-6-DIGIT";

typedef enum {
    state_idle = 0,
    state_pumping,
    state_pumping_ready,
    state_totalizer
} pump_state_t;

pump_state_t state = state_idle;

#define SCREEN_WIDTH 128 // OLED display width,  in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// declare an SSD1306 display object connected to I2C
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Signal strength icons stored in PROGMEM
const unsigned char PROGMEM signalStrengthIcons[][16] = {
  { // Signal strength 0 (no signal)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  },
  { // Signal strength 1 (weak signal)
    0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
    0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00
  },
  { // Signal strength 2
    0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00
  },
  { // Signal strength 3
    0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00
  },
  { // Signal strength 4 (strong signal)
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00
  }
};

// Function to draw the signal strength icon
void drawSignalStrength(int x, int y, int strength) {
  // Constrain the strength value between 0 and 4
  strength = constrain(strength, 0, 4);

  // Draw the icon at (x, y)
  oled.drawBitmap(x, y, signalStrengthIcons[strength], 8, 16, SSD1306_WHITE);
}

void setup(){

    board_init();   

    // initialize OLED display with address 0x3C for 128x64
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true);
    }

    oled.clearDisplay();
    oled.display();

    oled.setTextSize(1);          // text size
    oled.setTextColor(WHITE);     // text color
    oled.setCursor(0, 10);        // position to display
    oled.println("Configuring...");
    oled.println("Display Mode: ");
    // oled.println("");
    // oled.println(String(displayMode)); // text to display
    oled.display();               // show on OLED
    display_mode_curser_x = 0;
    display_mode_curser_y = 30;

    setupRotaryEncoder();
    sanki_init();
}

void loop(){  

    switch(device_state){
        case device_configuring:
            process_config();
            break;
        case device_running:
            switch (display_type)
            {
            case display_type_sanki_6digit:
                sanki_run();
                break;
            case display_type_censtar_6digit:
                censtar_6dig_run();   
                break;      
            default:
                // restarting
                Serial.println("Display Not Supported,Restarting...");
                wdt_enable(WDTO_15MS); // Set the watchdog timer to timeout in 15ms
                while (true);          // Wait for the WDT to reset the board
                break;
            }
            break;
        default:
            device_state = device_configuring;
            break;
    }    
}

void process_config(){

    static int16_t lastPosition = 0;
    // Get the current position of the rotary encoder
    int16_t position = getEncoderPosition();
    static const String * menu = pump_types;
    static int menu_idx = 0;
    if (position != lastPosition) {
        // Serial.print("Encoder Position: ");
        // Serial.println(position);
        
        lastPosition > position ? menu_idx-- : menu_idx++;
        menu_idx = menu_idx < 0 ? 0 : menu_idx;
        menu_idx = menu_idx >= pump_type_count ? pump_type_count-1 : menu_idx;

        Serial.println("Menu IDX = " + String(menu_idx));
       
        oled.fillRect(display_mode_curser_x, display_mode_curser_y, 128, 10, BLACK); // Clear the specific area
        oled.setCursor(10, 10);
        oled.setCursor(display_mode_curser_x, display_mode_curser_y);        // position to display        
        oled.println(String(menu[menu_idx]));
        oled.display();               // show on OLED

        lastPosition = position;        
    }

    if(LOW == digitalRead(c_pin_encoder_btn)){

      delay(50);
      display_type = menu_idx;
      device_state = device_running;

      oled.clearDisplay();
      oled.setTextSize(1);          // text size
      oled.setTextColor(WHITE);     // text color
      oled.setCursor(0, 10);        // position to display
      oled.println("Running...");
      oled.println("Display Mode: ");
      oled.println(String(menu[display_type]));
      oled.display();               // show on OLED

      switch (display_type){
        case display_type_sanki_6digit:
            sanki_init();
            break;
        case display_type_censtar_6digit:
            censtar_6dig_init();
            break;
        default:
            // restarting
            Serial.println("Display Not Supported,Restarting...");
            wdt_enable(WDTO_15MS); // Set the watchdog timer to timeout in 15ms
            while (true);          // Wait for the WDT to reset the board
            break;
      }
    }
}
