
#include "Encoder.h"

// Rotary Encoder Pins (Change these as needed)
#define ENCODER_PIN_A 18
#define ENCODER_PIN_B 19
#define ENCODER_BUTTON_PIN -1 // Set to -1 if no button is used

void handleEncoderInterruptB();
void handleEncoderInterruptA();

// Variables to store encoder state
volatile int16_t encoderPosition = 0;
volatile uint8_t lastState = 0;
volatile uint8_t encState = 0;     // Tracks the current state of the encoder
bool status_pinA_value = false;
bool status_pinB_value = false;

// Initialize rotary encoder
void setupRotaryEncoder() {
    pinMode(ENCODER_PIN_A, INPUT);
    pinMode(ENCODER_PIN_B, INPUT);
    if (ENCODER_BUTTON_PIN != -1) {
        pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
    }

    encState = (digitalRead(ENCODER_PIN_A) << 1) | digitalRead(ENCODER_PIN_B);
    encState = encState << 2;

    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), handleEncoderInterruptA, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), handleEncoderInterruptB, CHANGE);
}
void handleEncoderInterruptB() {
    uint8_t s = encState & 0b1100;  // Keep only the last two bits of the state
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();

    if (interruptTime - lastInterruptTime < 50) {
      return;
    }

    lastInterruptTime = interruptTime;  
    status_pinA_value = digitalRead(ENCODER_PIN_A);
    
    if (status_pinA_value) s |= 0b1; // Update bit 3
    if (status_pinB_value) s |= 0b10; // Update bit 3

    // Use a switch-case to handle the state transitions
    switch (s) {
        case 0: case 5: case 10: case 15:
            // No movement
            break;
        case 1: case 7: case 8: case 14:
            encoderPosition++;  // Clockwise movement
            break;
        case 2: case 4: case 11: case 13:
            encoderPosition--;  // Counter-clockwise movement
            break;
    }

    // Save the new state for the next iteration
    encState = (s << 2);  // Shift the state down to keep the last two bits
}
void handleEncoderInterruptA() {

    uint8_t s = encState & 0b1100;  // Keep only the last two bits of the state
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();

    if (interruptTime - lastInterruptTime < 50) {
      return;
    }

    lastInterruptTime = interruptTime;   
    status_pinB_value = digitalRead(ENCODER_PIN_B);

    if (status_pinA_value) s |= 0b1; // Update bit 3
    if (status_pinB_value) s |= 0b10; // Update bit 3

    // Use a switch-case to handle the state transitions
    switch (s) {
        case 0: case 5: case 10: case 15:
            // No movement
            break;
        case 1: case 7: case 8: case 14:
            // encoderPosition++;  // Clockwise movement
            break;
        case 2: case 4: case 11: case 13:
            // encoderPosition--;  // Counter-clockwise movement
            break;
    }

    // Save the new state for the next iteration
    encState = (s << 2);  // Shift the state down to keep the last two bits
}
// Get the current encoder position
int16_t getEncoderPosition() {
    noInterrupts();
    int16_t position = encoderPosition;
    interrupts();
    return position;
}