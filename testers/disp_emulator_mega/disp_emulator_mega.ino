#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/wdt.h>

#include "Encoder.h"
#include "sanki-6.h"
#include "censtar-6.h"
#include "pumps_extra.h"
#include "board_mega.h"
#include "control.h"

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
    display_type_censtar_7digit_cs,
    display_type_wayne_6digit_2,
    display_type_longfeng_8digit,
} display_type_t;

const int pump_type_count = 8;
const String pump_types[pump_type_count] = {
    "SANKI-6-DIGIT",
    "CENSTAR-6-DIGIT",
    "CENSTAR-7-DIGIT",
    "WAYNE-6-DIGIT",
    "HONGYANG-8-DIGIT",
    "CENSTAR-7-CS",
    "WAYNE-6-DIGIT-2",
    "LONGFENG-8-DIGIT",
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
    // show the current choice straight away (Up / Down change it, Enter starts it)
    oled.setCursor(display_mode_curser_x, display_mode_curser_y);
    oled.println(pump_types[0]);
    oled.display();

    setupRotaryEncoder();
    control_init();

    // after a MODE command the board restarts straight into that display type
    int8_t boot = control_boot_mode();
    switch (boot) {
      case CTL_SAN6: start_mode(display_type_sanki_6digit); break;
      case CTL_CEN6: start_mode(display_type_censtar_6digit); break;
      case CTL_CEN7: start_mode(display_type_censtar_7digit); break;
      case CTL_C7CS: start_mode(display_type_censtar_7digit_cs); break;
      case CTL_WAY6: start_mode(display_type_wayne_6digit); break;
      case CTL_HY8:  start_mode(display_type_hongyang_8digit); break;
      case CTL_WAY2: start_mode(display_type_wayne_6digit_2); break;
      case CTL_LF8:  start_mode(display_type_longfeng_8digit); break;
      default: break;
    }
}

// Start a display type: driver init + status screen.  Not implemented types restart into the menu.
void start_mode(int idx){
    display_type = (display_type_t)idx;
    device_state = device_running;
    switch (display_type){
      case display_type_sanki_6digit:
          sanki_init();
          break;
      case display_type_censtar_6digit:
          censtar_6dig_init();
          break;
      case display_type_censtar_7digit:
          censtar7_init(false);
          break;
      case display_type_censtar_7digit_cs:
          censtar7_init(true);
          break;
      case display_type_wayne_6digit:
          wayne6_init();
          break;
      case display_type_hongyang_8digit:
          hongyang8_init();
          break;
      case display_type_wayne_6digit_2:
          wayne2_init();
          break;
      case display_type_longfeng_8digit:
          longfeng8_init();
          break;
      default:
          Serial.println(F("ERR display type not supported, restarting to the menu"));
          control_restart(-1);
          break;
    }
    control_event("MODE", control_mode_name(emu.mode));
    control_screen(true);
}

void loop(){  

    control_poll();                 // serial commands + front-panel events
    switch(device_state){
        case device_configuring:
            process_config();
            break;
        case device_running:
            // Back button (or BUTTON BACK / MENU from the PC): restart into the display-type menu
            if (LOW == digitalRead(c_pin_btn_back) || control_take_back()) {
                control_event("MENU");
                control_restart(-1);
            }
            switch (display_type)
            {
            case display_type_sanki_6digit:
                sanki_run();
                break;
            case display_type_censtar_6digit:
                censtar_6dig_run();
                break;
            case display_type_censtar_7digit:
            case display_type_censtar_7digit_cs:
                censtar7_run();
                break;
            case display_type_wayne_6digit:
                wayne6_run();
                break;
            case display_type_hongyang_8digit:
                hongyang8_run();
                break;
            case display_type_wayne_6digit_2:
                wayne2_run();
                break;
            case display_type_longfeng_8digit:
                longfeng8_run();
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
    if (device_state == device_running) control_screen();
}

void process_config(){

    static int16_t lastPosition = 0;
    static int16_t remote_steps = 0;
    // Up / Down buttons, plus BUTTON UP / DOWN from the PC
    remote_steps += control_take_steps();
    control_take_back();                       // Back does nothing in the menu
    int16_t position = getEncoderPosition() + remote_steps;
    static const String * menu = pump_types;
    static int menu_idx = 0;
    if (position != lastPosition) {
        // Serial.print("Encoder Position: ");
        // Serial.println(position);
        
        lastPosition > position ? menu_idx-- : menu_idx++;
        menu_idx = menu_idx < 0 ? 0 : menu_idx;
        menu_idx = menu_idx >= pump_type_count ? pump_type_count-1 : menu_idx;

        control_event("MENU_IDX", String(menu_idx).c_str());
       
        oled.fillRect(display_mode_curser_x, display_mode_curser_y, 128, 10, BLACK); // Clear the specific area
        oled.setCursor(10, 10);
        oled.setCursor(display_mode_curser_x, display_mode_curser_y);        // position to display        
        oled.println(String(menu[menu_idx]));
        oled.display();               // show on OLED

        lastPosition = position;        
    }

    if(LOW == digitalRead(c_pin_encoder_btn) || control_take_enter()){
      delay(50);
      start_mode(menu_idx);
    }
}
