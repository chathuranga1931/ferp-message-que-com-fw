#include "Encoder.h"
#include "board_mega.h"

// Menu navigation with the front-panel Up / Down buttons (pins in board_mega.h).
// Keeps the old rotary-encoder interface: the "position" goes down one step per Up press
// and up one step per Down press, so the menu moves to the previous / next display type.

static const uint16_t c_debounce_ms = 30;

static volatile int16_t encoderPosition = 0;
static uint8_t last_up = HIGH;
static uint8_t last_down = HIGH;
static uint32_t last_change_ms = 0;

void setupRotaryEncoder() {
    pinMode(c_pin_btn_up, INPUT_PULLUP);
    pinMode(c_pin_btn_down, INPUT_PULLUP);
    pinMode(c_pin_btn_enter, INPUT_PULLUP);
    pinMode(c_pin_btn_back, INPUT_PULLUP);
    last_up = digitalRead(c_pin_btn_up);
    last_down = digitalRead(c_pin_btn_down);
}

// Polled from the menu loop: one step per press (on the press edge, debounced).
int16_t getEncoderPosition() {
    uint8_t up = digitalRead(c_pin_btn_up);
    uint8_t down = digitalRead(c_pin_btn_down);
    if (millis() - last_change_ms >= c_debounce_ms) {
        if (up == LOW && last_up == HIGH) { encoderPosition--; last_change_ms = millis(); }
        if (down == LOW && last_down == HIGH) { encoderPosition++; last_change_ms = millis(); }
        last_up = up;
        last_down = down;
    }
    return encoderPosition;
}
