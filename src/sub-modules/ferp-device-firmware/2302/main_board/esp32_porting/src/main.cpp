#define ARDUHAL_LOG_LEVEL (5)

#include <Arduino.h>

#include "app.h"

void setup(){
	app_init();
}

void loop(){
	app_run();
}

