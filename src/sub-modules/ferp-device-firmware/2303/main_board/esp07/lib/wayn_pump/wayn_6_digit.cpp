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
// static os_timer_t ptimer_dis_2;

// display tap 1
static uint8_t byte1_display_1 = 0, byte2_display_1 = 0;
static uint8_t data1_display_1[RX_BUF_SIZE] = {};
static uint8_t data2_display_1[RX_BUF_SIZE] = {};
static tuByte_t process_data1_display_1[RX_BUF_SIZE] = {};
static tuByte_t process_data2_display_1[RX_BUF_SIZE] = {};
static uint32_t rx_index_display_1 = 0;
// static display_data_t display_data_1 = {};
// static bool got_display_1 = false;
// // display tap 2
// static uint8_t byte1_display_2 = 0, byte2_display_2 = 0;
// static uint8_t data1_display_2[RX_BUF_SIZE] = {};
// static uint8_t data2_display_2[RX_BUF_SIZE] = {};
// static tuByte_t process_data1_display_2[RX_BUF_SIZE] = {};
// static tuByte_t process_data2_display_2[RX_BUF_SIZE] = {};
// static uint32_t rx_index_display_2 = 0;
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
	// ETS_GPIO_INTR_DISABLE();
	data1_display_1[rx_index_display_1] = byte1_display_1;
	data2_display_1[rx_index_display_1] = byte2_display_1;

	rx_index_display_1++;
	if (!(rx_index_display_1 < RX_BUF_SIZE))
		rx_index_display_1 = 0;

	os_timer_arm(&ptimer_dis_1, 20, 0);
	// ETS_GPIO_INTR_ENABLE();
}

// static void IRAM_ATTR bit_read_dis_2(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
// {
// 	ETS_GPIO_INTR_DISABLE();

// 	byte1_display_2 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_2) | (uint8_t)(byte1_display_2 << 1));
// 	byte2_display_2 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_2) | (uint8_t)(byte2_display_2 << 1));

// 	ETS_GPIO_INTR_ENABLE();
// }

// static void IRAM_ATTR byte_read_start_dis_2(void) // ICACHE_RAM_ATTR
// {
// 	ETS_GPIO_INTR_DISABLE();
// 	data1_display_2[rx_index_display_2] = byte1_display_2;
// 	data2_display_2[rx_index_display_2] = byte2_display_2;

// 	rx_index_display_2++;
// 	if (!(rx_index_display_2 < RX_BUF_SIZE))
// 		rx_index_display_2 = 0;

// 	os_timer_arm(&ptimer_dis_2, 20, 0);
// 	ETS_GPIO_INTR_ENABLE();
// }
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
	for (int i = 0; i < 14; i++)
	{
		if(ba[i].LS != i || bb[i].LS != i) // if packet ID is not matching with packet byte location, return invalid
		{
			data.flags.err_index = true;
		}
		if (ba[i].MS >= 10)
			ba[i].u8int = 0;
		if (bb[i].MS >= 10)
			bb[i].u8int = 0;
	}

	data.unit_price = (uint32_t)(ba[8].MS) * 100000 +
					 (uint32_t)(ba[9].MS) * 10000 +
					 (uint32_t)(ba[10].MS) * 1000 +
					 (uint32_t)(ba[11].MS) * 100 +
					 (uint32_t)(ba[12].MS) * 10 +
					 (uint32_t)(ba[13].MS);

	data.total_price = (uint32_t)(ba[1].MS) * 1000000 +
					  (uint32_t)(ba[2].MS) * 100000 +
					  (uint32_t)(ba[3].MS) * 10000 +
					  (uint32_t)(ba[4].MS) * 1000 +
					  (uint32_t)(ba[5].MS) * 100 +
					  (uint32_t)(ba[6].MS) * 10 +
					  (uint32_t)(ba[7].MS);

	data.volume_l = (uint32_t)(bb[1].MS) * 1000000 +
				   (uint32_t)(bb[2].MS) * 100000 +
				   (uint32_t)(bb[3].MS) * 10000 +
				   (uint32_t)(bb[4].MS) * 1000 +
				   (uint32_t)(bb[5].MS) * 100 +
				   (uint32_t)(bb[6].MS) * 10 +
				   (uint32_t)(bb[7].MS);
	int32_t gap = ((data.unit_price * data.volume_l)/1000) - data.total_price;
	if(abs(gap) > 1000) //if total price should match with unit_price*valume. Check for the tolerance
	{
		data.flags.err_gap = true;
	}

	if ((uint8_t)(bb[8].MS) == 1)
	{
		data.flags.start_stop = true;
	}
	else
	{
		data.flags.start_stop = false;
	}

	if ((uint8_t)(ba[0].MS) == 10)
	{
		data.flags.select_l = true;
	}
	else
	{
		data.flags.select_l = false;
	}

	if ((uint8_t)(bb[0].MS) == 12)
	{
		data.flags.select_p = true;
	}
	else
	{
		data.flags.select_p = false;
	}
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

	memset((void *)&(display_data->display_1.display_data), 0, sizeof(display_data_t));

	get_display_data(&display_data->display_1.display_data, process_data1_display_1, process_data2_display_1);
	display_data->got_display_1 = true;
}
// static void IRAM_ATTR data_rx_timeout_dis_2(void *arg)
// {
// 	const uint32_t biff_size = rx_index_display_2;
// 	rx_index_display_2 = 0;
// 	if (biff_size < DATA_SET_SIZE)
// 		return;

// 	memcpy(process_data1_display_2, data1_display_2, DATA_SET_SIZE);
// 	memcpy(process_data2_display_2, data2_display_2, DATA_SET_SIZE);

// 	memset((void *)&(display_data->display_2.display_data), 0, sizeof(display_data_t));
// 	get_display_data(&(display_data->display_2.display_data), process_data1_display_2, process_data2_display_2);
// 	display_data->got_display_2 = true;
// }

void display_wayne_6_digit_init(display_t *dis)
{
    display_data = dis;

	pinMode(DIS_ENB, OUTPUT);

	pinMode(D1_SCLK, INPUT);
	// pinMode(D1_RCLK, INPUT);
	pinMode(D1_SDATA1, INPUT);
	pinMode(D1_SDATA2, INPUT);
	// pinMode(D2_SCLK, INPUT);
	// pinMode(D2_RCLK, INPUT);
	// pinMode(D2_SDATA1, INPUT);
	// pinMode(D2_SDATA2, INPUT);

    attachInterrupt(digitalPinToInterrupt(D1_SCLK), bit_read_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D1_RCLK), byte_read_start_dis_1, RISING);
	// attachInterrupt(digitalPinToInterrupt(D2_SCLK), bit_read_dis_2, RISING);
	// attachInterrupt(digitalPinToInterrupt(D2_RCLK), byte_read_start_dis_2, RISING);

	os_timer_disarm(&ptimer_dis_1);
	os_timer_setfn(&ptimer_dis_1, (os_timer_func_t *)data_rx_timeout_dis_1, NULL);
	// os_timer_disarm(&ptimer_dis_2);
	// os_timer_setfn(&ptimer_dis_2, (os_timer_func_t *)data_rx_timeout_dis_2, NULL);

	// Enable display tapping signal inputs.
	digitalWrite(DIS_ENB, true);
}