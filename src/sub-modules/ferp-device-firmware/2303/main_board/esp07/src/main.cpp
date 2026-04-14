#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "math.h"
#include "censtar_6_digit.h"
#include "censtar_7_digit.h"
#include "censtar_8_digit.h"
#include "chinese_8_digit.h"
#include "wayn_6_digit.h"

#define SW_MAJOR 2
#define SW_MINOR 0


display_t display = {};

void setup()
{
	Serial.begin(115200);
	Serial.println("\r\n");
	Serial.println("Starting display tap....");
	LittleFS.begin();
	display.dis = DIS_CENSTAR_7_DIGIT;
	display.dis = DIS_CHINESE_8_DIGIT;

	switch (display.dis)
	{
	case DIS_CENSTAR_6_DIGIT:
		display_censtar_6_digit_init(&display);
		break;
	case DIS_CENSTAR_7_DIGIT:
		display_censtar_7_digit_init(&display);
		break;
	case DIS_CENSTAR_8_DIGIT:
		display_censtar_8_digit_init(&display);
		break;
	case DIS_CHINESE_8_DIGIT:
		display_chinese_8_digit_init(&display);
		break;
	case DIS_WAYNE_6_DIGIT:
		display_wayne_6_digit_init(&display);
		break;
	default:
		break;
	}

	const uint32_t packet_data_lenght = (sizeof(packet_data_t) - sizeof(packet_data_t::checksum)); // seize of whole packet - size of checksum
	memset((void *)&display.dummy, 0xAA, packet_data_lenght);
	display.dummy.sw_major = SW_MAJOR;
	display.dummy.sw_minor = SW_MINOR;
	display.dummy.display_id = 2;
	display.dummy.checksum = 0;
	for (size_t i = 0; i < packet_data_lenght; i++)
	{
		display.dummy.checksum += ((uint8_t *)&display.dummy)[i];
	}

	display.display_1.sw_major = SW_MAJOR;
	display.display_1.sw_minor = SW_MINOR;
	display.display_1.display_id = 0;

	display.display_2.sw_major = SW_MAJOR;
	display.display_2.sw_minor = SW_MINOR;
	display.display_2.display_id = 1;

	Serial.flush();	
	// send_packet(&display.dummy); // send dummy data
	// time_prev = millis();
}

void loop()
{
	// recieve_serial(&display);
	// send_serial(&display);	
}
// ICACHE_FLASH_ATTR
