#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "math.h"
#include "device.h"

#define GP_IN(pin) GPIO_INPUT_GET(GPIO_ID_PIN(pin)) // GP_FAST_READ(pin, GPIO_IN_REG)
#define RX_BUF_SIZE 30
#define DATA_SET_SIZE 14

#define DIS_ENB 15

#define D1_SCLK 14
#define D1_RCLK 12
#define D1_SDATA1 13
#define D1_SDATA2 16

#define D2_SCLK 4
#define D2_RCLK 5
#define D2_SDATA1 2
#define D2_SDATA2 0

#define c_pin_sdata1_read_dis_1 GPIO_INPUT_GET(GPIO_ID_PIN(13))		  // pin number 13
#define c_pin_sdata2_read_dis_1 (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1) // because pin number is 16

#define c_pin_sdata1_read_dis_2 GPIO_INPUT_GET(GPIO_ID_PIN(2)) // pin number 2
#define c_pin_sdata2_read_dis_2 GPIO_INPUT_GET(GPIO_ID_PIN(0)) // pin number 0

typedef enum
{
	IDX_SELECT_L = 0,

	IDX_TOTAL_START = 1,
	IDX_TOTAL_6 = IDX_TOTAL_START,
	IDX_TOTAL_5,
	IDX_TOTAL_4,
	IDX_TOTAL_3,
	IDX_TOTAL_2,
	IDX_TOTAL_1,
	IDX_TOTAL_0,
	IDX_TOTAL_SIZE,

	IDX_UNIT_START = 8,
	IDX_UNIT_5 = IDX_UNIT_START,
	IDX_UNIT_4,
	IDX_UNIT_3,
	IDX_UNIT_2,
	IDX_UNIT_1,
	IDX_UNIT_0,
	IDX_UNIT_SIZE
} ba_byte_index_t;

typedef enum
{
	IDX_SELECT_P = 0,
	
	IDX_VOLUME_START = 1,
	IDX_VOLUME_6 = IDX_VOLUME_START,
	IDX_VOLUME_5,
	IDX_VOLUME_4,
	IDX_VOLUME_3,
	IDX_VOLUME_2,
	IDX_VOLUME_1,
	IDX_VOLUME_0,
	IDX_VOLUME_SIZE,
	
	IDX_START_STOP = 8,
} bb_byte_index_t;

typedef union
{
	struct
	{
		uint8_t LS : 4; // first 4 bits
		uint8_t MS : 4; // last 4 bits
	};
	uint8_t u8int;
} tuByte_t;

static const uint32_t packet_data_size = sizeof(packet_data_t);
static const uint32_t packet_data_lenght = (sizeof(packet_data_t) - sizeof(packet_data_t::checksum)); // seize of whole packet - size of checksum

static os_timer_t ptimer_dis_1;
static os_timer_t ptimer_dis_2;

// display tap 1
static uint8_t byte1_display_1 = 0, byte2_display_1 = 0;
static uint8_t data1_display_1[RX_BUF_SIZE] = {};
static uint8_t data2_display_1[RX_BUF_SIZE] = {};
static tuByte_t process_data1_display_1[RX_BUF_SIZE] = {};
static tuByte_t process_data2_display_1[RX_BUF_SIZE] = {};
static uint32_t rx_index_display_1 = 0;
// static display_data_t display_data_1 = {};
// static bool got_display_1 = false;
// display tap 2
static uint8_t byte1_display_2 = 0, byte2_display_2 = 0;
static uint8_t data1_display_2[RX_BUF_SIZE] = {};
static uint8_t data2_display_2[RX_BUF_SIZE] = {};
static tuByte_t process_data1_display_2[RX_BUF_SIZE] = {};
static tuByte_t process_data2_display_2[RX_BUF_SIZE] = {};
static uint32_t rx_index_display_2 = 0;
// static display_data_t display_data_2 = {};
// static bool got_display_2 = false;

// static packet_data_t send_data_1 = {}, send_data_2 = {}, dummy_data = {};
static display_t *display_data = NULL;

static void IRAM_ATTR bit_read_dis_1(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_1 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_1) | (uint8_t)(byte1_display_1 << 1));
	byte2_display_1 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_1) | (uint8_t)(byte2_display_1 << 1));

	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR byte_read_start_dis_1(void) // ICACHE_RAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();
	data1_display_1[rx_index_display_1] = byte1_display_1;
	data2_display_1[rx_index_display_1] = byte2_display_1;

	rx_index_display_1++;
	if (!(rx_index_display_1 < RX_BUF_SIZE))
		rx_index_display_1 = 0;

	os_timer_arm(&ptimer_dis_1, 20, 0);
	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR bit_read_dis_2(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_2 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_2) | (uint8_t)(byte1_display_2 << 1));
	byte2_display_2 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_2) | (uint8_t)(byte2_display_2 << 1));

	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR byte_read_start_dis_2(void) // ICACHE_RAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();
	data1_display_2[rx_index_display_2] = byte1_display_2;
	data2_display_2[rx_index_display_2] = byte2_display_2;

	rx_index_display_2++;
	if (!(rx_index_display_2 < RX_BUF_SIZE))
		rx_index_display_2 = 0;

	os_timer_arm(&ptimer_dis_2, 20, 0);
	ETS_GPIO_INTR_ENABLE();
}
/*
Example packet
		Data 1 (ba)							Data 2 (bb)			
Decimal	Binary		MSB	LSB 	Decimal	Binary		MSB	LSB 4
240		11110000	15	0		240		11110000	15	0
241		11110001	15	1		241		11110001	15	1
242		11110010	15	2		242		11110010	15	2
19		00010011	1	3		243		11110011	15	3
4		00000100	0	4		4		00000100	0	4
21		00010101	1	5		37		00100101	2	5
38		00100110	2	6		38		00100110	2	6
87		01010111	5	7		87		01010111	5	7
248		11111000	15	8		168		10101000	10	8
73		01001001	4	9		169		10101001	10	9
90		01011010	5	10		26		00011010	1	10
11		00001011	0	11		171		10101011	10	11
12		00001100	0	12		252		11111100	15	12
13		00001101	0	13		173		10101101	10	13
* Packet has two 14 byte arrays.
* LSB (4 bits) of a byte represents byte index. If index is not matching, ignore
* MSB (4 bits) represent a decimal number and symbols. 
*	- Value 0 to 9 represents decimal 0 to 9.
*	- 10 -> L
*	- 11 -> H
*	- 12 -> P
*	- 13 -> A
*	- 14 -> -
*	- 15 -> Blank
*
*/

static void get_display_data(display_data_t *dd, tuByte_t *ba, tuByte_t *bb)
{
	display_data_t data = {};

	if ((uint8_t)(bb[IDX_START_STOP].MS) == 1)
	{
		data.flags.start_stop = true;
	}
	else
	{
		data.flags.start_stop = false;
	}

	if ((uint8_t)(ba[IDX_SELECT_L].MS) == 10)
	{
		data.flags.select_l = true;
	}
	else
	{
		data.flags.select_l = false;
	}

	if ((uint8_t)(bb[IDX_SELECT_P].MS) == 12)
	{
		data.flags.select_p = true;
	}
	else
	{
		data.flags.select_p = false;
	}

	for (int i = 0; i < 14; i++)
	{

		if(ba[i].LS != i || bb[i].LS != i) // if packet ID is not matching with packet byte location, mark invalid
		{
			data.flags.err_index = true;
		}

		if(IDX_TOTAL_START < i  && i < IDX_TOTAL_SIZE)
		{
			if(ba[i].LS != i)
				data.flags.err_totprice = true;
		}else if(IDX_UNIT_START < i && i < IDX_UNIT_SIZE)
		{
			if(ba[i].LS != i)
				data.flags.err_unitprice = true;
		}
		
		if (IDX_VOLUME_START < i && i < IDX_VOLUME_SIZE)
		{
			if(bb[i].LS != i)
				data.flags.err_volume = true;
		}
	
		if (ba[i].MS >= 10)
			ba[i].MS = 0;
		if (bb[i].MS >= 10)
			bb[i].MS = 0;
	}

	data.unit_price = 	(uint32_t)(ba[IDX_UNIT_5].MS) * 100000 +
					 	(uint32_t)(ba[IDX_UNIT_4].MS) * 10000 +
					 	(uint32_t)(ba[IDX_UNIT_3].MS) * 1000 +
					 	(uint32_t)(ba[IDX_UNIT_2].MS) * 100 +
					 	(uint32_t)(ba[IDX_UNIT_1].MS) * 10 +
					 	(uint32_t)(ba[IDX_UNIT_0].MS);

	data.total_price =	(uint32_t)(ba[IDX_TOTAL_6].MS) * 1000000 +
					  	(uint32_t)(ba[IDX_TOTAL_5].MS) * 100000 +
					  	(uint32_t)(ba[IDX_TOTAL_4].MS) * 10000 +
					  	(uint32_t)(ba[IDX_TOTAL_3].MS) * 1000 +
					  	(uint32_t)(ba[IDX_TOTAL_2].MS) * 100 +
					  	(uint32_t)(ba[IDX_TOTAL_1].MS) * 10 +
					  	(uint32_t)(ba[IDX_TOTAL_0].MS);

	data.volume_l = 	(uint32_t)(bb[IDX_VOLUME_6].MS) * 1000000 +
				   		(uint32_t)(bb[IDX_VOLUME_5].MS) * 100000 +
				   		(uint32_t)(bb[IDX_VOLUME_4].MS) * 10000 +
				   		(uint32_t)(bb[IDX_VOLUME_3].MS) * 1000 +
				   		(uint32_t)(bb[IDX_VOLUME_2].MS) * 100 +
				   		(uint32_t)(bb[IDX_VOLUME_1].MS) * 10 +
				   		(uint32_t)(bb[IDX_VOLUME_0].MS);
	int32_t gap = ((data.unit_price * data.volume_l)/1000) - data.total_price;
	if(abs(gap) > 1000) //if total price should match with unit_price*valume. Check for the tolerance
	{
		data.flags.err_gap = true;
	}

	Serial.println("unit=" + String(data.unit_price / 100.0) + " total=" + String(data.total_price / 100.0) + " volume=" + String(data.volume_l / 1000.0) + " " + (data.flags.start_stop ? "start" : "stop") + " select_p=" + (data.flags.select_p ? "true" : "false") + " select_l=" + (data.flags.select_l ? "true" : "false"));
	Serial.println();

	memcpy((void*)dd, (void*)&data, sizeof(display_data_t));
}

static void IRAM_ATTR data_rx_timeout_dis_1(void *arg)
{
	const uint32_t biff_size = rx_index_display_1;
	rx_index_display_1 = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_1, data1_display_1, DATA_SET_SIZE);
	memcpy(process_data2_display_1, data2_display_1, DATA_SET_SIZE);

	// memset((void *)&(display_data->display_1.display_data), 0, sizeof(display_data_t));

	get_display_data(&display_data->display_1.display_data, process_data1_display_1, process_data2_display_1);
	display_data->got_display_1 = true;

	// double total_price_expected = display_data->display_1.unit_price / 100.0 * display_data->display_1.volume_l / 1000.0;
	// double gap = total_price_expected - display_data->display_1.total_price / 100.0;
	// double modulo_gap = gap < 0 ? gap * (-1) : gap;

	// // Serial.println("Gap=" + String(gap));
	// Serial.println("unit=" + String(display_data->display_1.display_data.unit_price / 100.0) + " total=" + String(display_data->display_1.display_data.total_price / 100.0) + " volume=" + String(display_data->display_1.display_data.volume_l / 1000.0) + " " + (display_data->display_1.display_data.flags.start_stop ? "start" : "stop") + " select_p=" + (display_data->display_1.display_data.flags.select_p ? "true" : "false") + " select_l=" + (display_data->display_1.display_data.flags.select_l ? "true" : "false"));
	// Serial.println();
}
static void IRAM_ATTR data_rx_timeout_dis_2(void *arg)
{
	const uint32_t biff_size = rx_index_display_2;
	rx_index_display_2 = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_2, data1_display_2, DATA_SET_SIZE);
	memcpy(process_data2_display_2, data2_display_2, DATA_SET_SIZE);

	memset((void *)&(display_data->display_2.display_data), 0, sizeof(display_data_t));
	get_display_data(&(display_data->display_2.display_data), process_data1_display_2, process_data2_display_2);
	display_data->got_display_2 = true;
}
// /*
//  * 0xff,0xff,0x4e,0x0b,0x10,0x15,0xfe,0x00,0x30,0xfe,0x01,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
//  * 0xff,0xff,0x4e,0x0b,0x10,0x15,  0xff   ,0x30,  0xfe   ,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
//  */
// static void send_packet(packet_data_t *pckt)
// {
// 	Serial.write(0xFF);
// 	Serial.write(0xFF);
// 	for (size_t i = 0; i < packet_data_size; i++)
// 	{
// 		if (((uint8_t *)pckt)[i] == 0xFF)
// 		{
// 			Serial.write(0xFE);
// 			Serial.write(0x00);
// 		}
// 		else if (((uint8_t *)pckt)[i] == 0xFE)
// 		{
// 			Serial.write(0xFE);
// 			Serial.write(0x01);
// 		}
// 		else
// 		{
// 			Serial.write(((uint8_t *)pckt)[i]);
// 		}
// 	}
// 	Serial.write(0xFF);
// }

// static void send_data_display_1()
// {
// 	// memcpy((void *)&display_data..display_data, (void *)&display_data_1, sizeof(display_data_t));
// 	send_data_1.sw_major = get_sw_major();
// 	send_data_1.sw_minor = get_sw_minor();
// 	send_data_1.display_id = 0;
// 	send_data_1.checksum = 0;
// 	for (size_t i = 0; i < packet_data_lenght; i++)
// 	{
// 		send_data_1.checksum += ((uint8_t *)&send_data_1)[i];
// 	}
// 	send_packet(&send_data_1);
// }
// static void send_data_display_2()
// {
// 	memcpy((void *)&send_data_2.display_data, (void *)&display_data_2, sizeof(display_data_t));
// 	send_data_2.sw_major = get_sw_major();
// 	send_data_2.sw_minor = get_sw_minor();
// 	send_data_2.display_id = 1;
// 	send_data_2.checksum = 0;
// 	for (size_t i = 0; i < packet_data_lenght; i++)
// 	{
// 		send_data_2.checksum += ((uint8_t *)&send_data_2)[i];
// 	}
// 	send_packet(&send_data_2);
// }

// static inline void send_dummy()
// {
// 	send_packet(&dummy_data);
// }

// static bool send_data()
// {
//     const bool state = got_display_1 | got_display_2;
// 	if (got_display_1)
// 	{
// 		send_data_display_1();
// 		got_display_1 = false;
// 	}
// 	if (got_display_2)
// 	{
// 		send_data_display_2();
// 		got_display_2 = false;
// 	}
//     return state;
// }

void display_chinese_8_digit_init(display_t *dis)
{
    display_data = dis;

	pinMode(DIS_ENB, OUTPUT);
	pinMode(D1_SCLK, INPUT);
	pinMode(D1_RCLK, INPUT);
	pinMode(D1_SDATA1, INPUT);
	pinMode(D1_SDATA2, INPUT);
	pinMode(D2_SCLK, INPUT);
	pinMode(D2_RCLK, INPUT);
	pinMode(D2_SDATA1, INPUT);
	pinMode(D2_SDATA2, INPUT);

    attachInterrupt(digitalPinToInterrupt(D1_SCLK), bit_read_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D1_RCLK), byte_read_start_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_SCLK), bit_read_dis_2, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_RCLK), byte_read_start_dis_2, RISING);

	os_timer_disarm(&ptimer_dis_1);
	os_timer_setfn(&ptimer_dis_1, (os_timer_func_t *)data_rx_timeout_dis_1, NULL);
	os_timer_disarm(&ptimer_dis_2);
	os_timer_setfn(&ptimer_dis_2, (os_timer_func_t *)data_rx_timeout_dis_2, NULL);

	// memset((void *)&dummy_data, 0xAA, packet_data_lenght);
	// dummy_data.sw_major = get_sw_major();
	// dummy_data.sw_minor = get_sw_minor();
	// dummy_data.display_id = 2;
	// dummy_data.checksum = 0;
	// for (size_t i = 0; i < packet_data_lenght; i++)
	// {
	// 	dummy_data.checksum += ((uint8_t *)&dummy_data)[i];
	// }
	// dis->send_dummy = send_dummy;
	// dis->send_display_data = send_data;

    // send_dummy();
	// Enable display tapping signal inputs.
	Serial.println("Starting Chinese 8 bit Display");
	digitalWrite(DIS_ENB, true);
}
