#pragma once

// Remote control over the USB serial port (115200 baud) and the status screen.
//
// Line protocol (ASCII, one command per line, case-insensitive, ends with \n or \r\n):
//   PING                         -> OK PONG
//   HELP                         -> OK HELP <command list>
//   STATUS                       -> OK STATUS mode=.. nozzle=.. pump=.. vol_ml=.. price=.. total=.. step_ml=.. ctl=..
//   MODE SAN6|CEN6|CEN7|C7CS|WAY6|HY8|WAY2 -> OK MODE <m>  start that display type (restarts the board when switching)
//   MENU                         -> OK MENU           back to the display-type menu (same as the Back button)
//   NOZZLE 1|0|AUTO              -> OK NOZZLE <v>     nozzle up / down from the PC; AUTO = back to the switch (pin 52)
//   PUMP 1|0|AUTO                -> OK PUMP <v>       pumping on / off from the PC; AUTO = back to the button (pin 50)
//   PRICE <unit price x100>      -> OK PRICE <n>      e.g. PRICE 42300 = 423.00
//   VOLUME <ml>                  -> OK VOLUME <n>     current sale volume
//   STEP <ml>                    -> OK STEP <n>       volume added every 100 ms while pumping
//   BUTTON UP|DOWN|ENTER|BACK    -> OK BUTTON <b>     press a front-panel button from the PC
//   DEBUG 1|0                    -> OK DEBUG <v>      the drivers' raw buffer printouts (off by default)
// Errors: ERR <reason>.   Unprompted events: EVT <what> (READY, MODE, MENU, BUTTON, NOZZLE, PUMP, ZERO).

#include "Arduino.h"

enum ctl_mode_t : uint8_t { CTL_MENU = 0, CTL_SAN6, CTL_CEN6, CTL_CEN7, CTL_C7CS, CTL_WAY6, CTL_HY8, CTL_WAY2 };

struct emu_state_t {
    ctl_mode_t mode;
    bool nozzle;            // effective nozzle state (switch or PC)
    bool pumping;           // effective pumping state (button or PC)
    int8_t remote_nozzle;   // -1 = from the switch, 0 / 1 = set by the PC
    int8_t remote_pumping;  // -1 = from the button, 0 / 1 = set by the PC
    uint32_t volume_ml;     // current sale volume
    uint32_t unit_price;    // unit price x100
    uint32_t step_ml;       // volume added every 100 ms while pumping
    bool price_set;         // PRICE given by the PC (drivers keep it instead of their default)
    bool step_set;          // STEP given by the PC
    bool dirty;             // values changed by the PC: the driver rebuilds its display buffer
    bool debug;             // raw buffer printouts on the serial port
    char last_event[22];    // shown on the screen, e.g. "PUMP button"
};

extern emu_state_t emu;

void control_init();
void control_poll();                          // read serial commands + front-panel edges (call every loop)
bool control_nozzle(bool switch_pressed);     // effective nozzle: PC override, else the switch
bool control_pumping(bool button_pressed);    // effective pumping: PC override, else the button
int16_t control_take_steps();                 // menu steps requested by BUTTON UP / DOWN
bool control_take_enter();                    // BUTTON ENTER requested
bool control_take_back();                     // BUTTON BACK / MENU requested
int8_t control_boot_mode();                   // display type to start after a MODE restart, -1 = menu
void control_restart(int8_t start_mode);      // restart the board, then start that display type (-1 = menu)
void control_event(const char *what, const char *detail = nullptr);
void control_screen(bool force = false);      // running-mode status screen (at most 4 updates / s)
uint32_t control_total();                     // sale total x100
const char *control_mode_name(int8_t mode);   // SAN6, CEN6, CEN7, C7CS, WAY6, HY8, WAY2, MENU
