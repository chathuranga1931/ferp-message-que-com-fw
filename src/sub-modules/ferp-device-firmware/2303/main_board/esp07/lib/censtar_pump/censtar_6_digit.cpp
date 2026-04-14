#include <Arduino.h>
#include "device.h"

static display_t *display_data = NULL;

void display_censtar_6_digit_init(display_t *dis)
{
    display_data = dis;
}
