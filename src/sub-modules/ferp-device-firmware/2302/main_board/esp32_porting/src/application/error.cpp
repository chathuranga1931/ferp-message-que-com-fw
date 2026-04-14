

#include "Arduino.h"

#include "error.h"
#include "logger.h"

void error_handler(ret_t ret){

	if(ret == ret_Success){
		return;
	}

	logger.log("Error =" + String(ret));
	ESP.restart();
}