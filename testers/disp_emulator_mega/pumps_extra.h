#pragma once

// CENSTAR-7, CENSTAR-7-CS, WAYNE-6, HONGYANG-8, WAYNE-6 type 2 and LONGFENG-8 display buses.
// Ported from esp-device-firmware-new/display_simulator/{censtar_7_digit, censtar_7_digit_cs,
// wayne_6_digit, hongyang_8_digit_type2}, mapped onto this board's outputs:
//   simulator SCLK -> pin 2 (CLOCK_1), SDATA1 -> pin 3 (DATA_1), SDATA2 -> pin 5 (DATA_2), RCLK -> pin 6 (CS_1).

#include "Arduino.h"

void censtar7_init(bool cs_variant);
void censtar7_run();
void wayne6_init();
void wayne6_run();
void hongyang8_init();
void hongyang8_run();
void wayne2_init();
void wayne2_run();
void longfeng8_init();
void longfeng8_run();
