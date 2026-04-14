#include <Arduino.h>
#include "device.h"
static const uint32_t packet_data_size = sizeof(packet_data_t);
static const uint32_t packet_data_lenght = (sizeof(packet_data_t) - sizeof(packet_data_t::checksum)); // seize of whole packet - size of checksum

unsigned long time_prev;
/*
 * 0xff,0xff,0x4e,0x0b,0x10,0x15,0xfe,0x00,0x30,0xfe,0x01,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
 * 0xff,0xff,0x4e,0x0b,0x10,0x15,  0xff   ,0x30,  0xfe   ,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
 */
void send_packet(packet_data_t *pckt)
{
	Serial.write(0xFF);
	Serial.write(0xFF);
	for (size_t i = 0; i < sizeof(packet_data_t); i++)
	{
		if (((uint8_t *)pckt)[i] == 0xFF)
		{
			Serial.write(0xFE);
			Serial.write(0x00);
		}
		else if (((uint8_t *)pckt)[i] == 0xFE)
		{
			Serial.write(0xFE);
			Serial.write(0x01);
		}
		else
		{
			Serial.write(((uint8_t *)pckt)[i]);
		}
	}
	Serial.write(0xFF);
}

void send_data_display(packet_data_t *pckt)
{
	// memcpy((void *)&send_data_1.display_data, (void *)&display_data_1, sizeof(display_data_t));
	// send_data_1.sw_major = get_sw_major();
	// send_data_1.sw_minor = get_sw_minor();
	// send_data_1.display_id = 0;
	pckt->checksum = 0;
	for (size_t i = 0; i < packet_data_lenght; i++)
	{
		pckt->checksum += ((uint8_t *)pckt)[i];
	}
	send_packet(pckt);
}

void send_serial(display_t *dis)
{
	const time_t time_now = millis();

	if (dis->got_display_1)
	{
		send_data_display(&dis->display_1);
		dis->got_display_1 = false;
		time_prev = time_now;
	}
	if (dis->got_display_2)
	{
		send_data_display(&dis->display_2);
		dis->got_display_2 = false;
		time_prev = time_now;
	}

	// sending dummy data to indicate i am alive
	if ((time_now - time_prev) > 5000)
	{
		send_packet(&dis->dummy);
		time_prev = time_now;
	}
}

void recieve_serial(display_t *dis)
{
	if(Serial.available())
	{
		
	}
}