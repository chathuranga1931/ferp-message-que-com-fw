#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "math.h"
#include "device.h"

#define GP_IN(pin) GPIO_INPUT_GET(GPIO_ID_PIN(pin)) // GP_FAST_READ(pin, GPIO_IN_REG)
#define RX_BUF_SIZE 41
#define DATA_SET_SIZE 20

#define DIS_ENB 15

#define D1_SCLK 14
#define D1_RCLK 12
#define D1_SDATA1 13

#define D2_SCLK 4
#define D2_RCLK 5
#define D2_SDATA1 2

#define D1_SDATA2 16 // Not used
#define D2_SDATA2 0  // Not used

#define c_pin_sdata1_read_dis_1 GPIO_INPUT_GET(GPIO_ID_PIN(13))		  // pin number 13
// #define c_pin_sdata2_read_dis_1 (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1) // because pin number is 16

#define c_pin_sdata1_read_dis_2 GPIO_INPUT_GET(GPIO_ID_PIN(2)) // pin number 2
// #define c_pin_sdata2_read_dis_2 GPIO_INPUT_GET(GPIO_ID_PIN(0)) // pin number 0

typedef enum
{
	IDX_UNIT_0 = 0,
	IDX_UNIT_1,
	IDX_UNIT_2,
	IDX_UNIT_3,
	IDX_UNIT_4,
	IDX_UNIT_SIZE,
	IDX_TOTAL_0 = 5,
	IDX_TOTAL_1,
	IDX_TOTAL_2,
	IDX_TOTAL_3,
	IDX_TOTAL_4,
	IDX_TOTAL_5,
	IDX_TOTAL_6,
	IDX_TOTAL_SIZE,
	IDX_VOLUME_0 = 12,
	IDX_VOLUME_1,
	IDX_VOLUME_2,
	IDX_VOLUME_3,
	IDX_VOLUME_4,
	IDX_VOLUME_5,
	IDX_VOLUME_6,
	IDX_VOLUME_SIZE,
} byte_index_t;

typedef union
{
	struct
	{
		uint16_t LSB : 4; // first 4 bits
        uint16_t MDB : 4; // mid 4 bits
		uint16_t MSB : 4; // last 4 bits
        uint16_t RES : 4; // rest 4 bits
	};
	uint16_t u16int;
} tu_2byte_t;

static const size_t packet_data_size = sizeof(packet_data_t);
static const size_t packet_data_lenght = (sizeof(packet_data_t) - sizeof(packet_data_t::checksum)); // seize of whole packet - size of checksum

static os_timer_t ptimer_dis_1;
static os_timer_t ptimer_dis_2;

// display tap 1
static uint16_t byte1_display_1 = 0;
static uint16_t data1_display_1[RX_BUF_SIZE] = {};
static tu_2byte_t process_data1_display_1[DATA_SET_SIZE] = {};
static size_t rx_index_display_1 = 0;
// display tap 2
static uint16_t byte1_display_2 = 0;
static uint16_t data1_display_2[RX_BUF_SIZE] = {};
static tu_2byte_t process_data1_display_2[DATA_SET_SIZE] = {};
static size_t rx_index_display_2 = 0;

static display_t *display_data = NULL;

static void IRAM_ATTR bit_read_dis_1(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_1 = (uint16_t)((uint16_t)(c_pin_sdata1_read_dis_1) | (uint16_t)(byte1_display_1 << 1));
	// byte2_display_1 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_1) | (uint8_t)(byte2_display_1 << 1));

	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR read_end_12bit_dis_1(void) // ICACHE_RAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();
	data1_display_1[rx_index_display_1] = byte1_display_1;
	byte1_display_1 = 0;
	// data2_display_1[rx_index_display_1] = byte2_display_1;

	rx_index_display_1++;
	if (!(rx_index_display_1 < RX_BUF_SIZE))
		rx_index_display_1 = 0;

	os_timer_arm(&ptimer_dis_1, 20, 0);
	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR bit_read_dis_2(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_2 = (uint16_t)((uint16_t)(c_pin_sdata1_read_dis_2) | (uint16_t)(byte1_display_2 << 1));
	// byte2_display_2 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_2) | (uint8_t)(byte2_display_2 << 1));

	ETS_GPIO_INTR_ENABLE();
}

static void IRAM_ATTR read_end_12bit_dis_2(void) // ICACHE_RAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();
	data1_display_2[rx_index_display_2] = byte1_display_2;
	byte1_display_2 = 0;
	// data2_display_2[rx_index_display_2] = byte2_display_2;

	rx_index_display_2++;
	if (!(rx_index_display_2 < RX_BUF_SIZE))
		rx_index_display_2 = 0;

	os_timer_arm(&ptimer_dis_2, 20, 0);
	ETS_GPIO_INTR_ENABLE();
}
/*
Example packet
Hex     Binary               RES MSB MDB LSB 
0005	0000 0000 0000 0101  0   0   0   5
0015    0000 0000 0001 0101  0   0   1   5
0625    0000 0110 0010 0101  0   6   2   5
0535    0000 0101 0011 0101  0   5   3   5
0345    0000 0011 0100 0101  0   3   4   5
0355    0000 0011 0101 0101  0   3   5   5
0865    0000 1000 0110 0101  0   8   6   5
0075    0000 0000 0111 0101  0   0   7   5
0685    0000 0110 1000 0101  0   6   8   5
0895    0000 1000 1001 0101  0   8   9   5
05A5    0000 
0FB5    0000 
03C5    0000 
06D5    0000 
04E5    0000 
06F3    0000 
01F0    0000 
0FF1    0000 
0FF2    0000 

* Packet has two 19 byte arrays.
* MDB Mid  (4 bits) of the 12bit represents byte index. If index is not matching, make error relevant to parameter
* MSB Left (4 bits) represent the decimal number for display. 
* LSB Right (4 bits) represents the index after exeeding Mid 4 bytes.
*	- Value 0 to 9 represents decimal 0 to 9.
*	- 10 -> 
*	- 11 -> 
*	- 12 -> 
*	- 13 -> 
*	- 14 -> 
*	- 15 -> Blank
*
*/

static void get_display_data(display_data_t *dd, tu_2byte_t *ab)
{
	display_data_t data = {};
	// if ((uint8_t)(bb[8].MS) == 1)
	// {
	// 	data.flags.start_stop = true;
	// }
	// else
	// {
	// 	data.flags.start_stop = false;
	// }

	// if ((uint8_t)(ab[0].MS) == 10)
	// {
	// 	data.flags.select_l = true;
	// }
	// else
	// {
	// 	data.flags.select_l = false;
	// }

	// if ((uint8_t)(bb[0].MS) == 12)
	// {
	// 	data.flags.select_p = true;
	// }
	// else
	// {
	// 	data.flags.select_p = false;
	// }

	for (size_t i = 0; i < 19; i++)
	{
		if(i < 16) //below
		{
			if(ab[i].MDB != i) //if packet ID is not matching with packet byte location, mark invalid
				data.flags.err_index = true;
		}
		else
		{
			if(ab[i].MDB != 0xF || ab[i].LSB != (i-0xF))
				data.flags.err_index = true;
		}
		
		if(i < IDX_UNIT_SIZE) //check error for unit price
		{
			if(ab[i].MDB != i)
				data.flags.err_unitprice = true;
		}
		else if (i < IDX_TOTAL_SIZE) //check error for totaprice
		{
			if(ab[i].MDB != i)
				data.flags.err_totprice = true;
		}
		else if(i < IDX_VOLUME_SIZE)  // check error for volume
		{
			if(i < 16) //below
			{
				if(ab[i].MDB != i) //if packet ID is not matching with packet byte location, mark invalid
					data.flags.err_volume = true;
			}
			else
			{
				if(ab[i].MDB != 0xF || ab[i].LSB != (i - 16))
					data.flags.err_volume = true;
			}
		}
		if (ab[i].MSB >= 10)
			ab[i].MSB = 0;
	}

	data.unit_price = 	(uint32_t)(ab[IDX_UNIT_4].MSB) * 10000 +
					 	(uint32_t)(ab[IDX_UNIT_3].MSB) * 1000 +
					 	(uint32_t)(ab[IDX_UNIT_2].MSB) * 100 +
					 	(uint32_t)(ab[IDX_UNIT_1].MSB) * 10 +
					 	(uint32_t)(ab[IDX_UNIT_0].MSB);

	data.total_price = 	(uint32_t)(ab[IDX_TOTAL_6].MSB) * 1000000 +
					  	(uint32_t)(ab[IDX_TOTAL_5].MSB) * 100000 +
					  	(uint32_t)(ab[IDX_TOTAL_4].MSB) * 10000 +
					  	(uint32_t)(ab[IDX_TOTAL_3].MSB) * 1000 +
					  	(uint32_t)(ab[IDX_TOTAL_2].MSB) * 100 +
					  	(uint32_t)(ab[IDX_TOTAL_1].MSB) * 10 +
					  	(uint32_t)(ab[IDX_TOTAL_0].MSB);

	data.volume_l = 	(uint32_t)(ab[IDX_VOLUME_6].MSB) * 1000000 +
						(uint32_t)(ab[IDX_VOLUME_5].MSB) * 100000 +
						(uint32_t)(ab[IDX_VOLUME_4].MSB) * 10000 +
				   		(uint32_t)(ab[IDX_VOLUME_3].MSB) * 1000 +
				   		(uint32_t)(ab[IDX_VOLUME_2].MSB) * 100 +
				   		(uint32_t)(ab[IDX_VOLUME_1].MSB) * 10 +
				   		(uint32_t)(ab[IDX_VOLUME_0].MSB);
	const int32_t gap = ((data.unit_price * data.volume_l)/1000) - data.total_price;
	if(abs(gap) > 1000) //if total price should match with unit_price*valume. Check for the tolerance
	{
		data.flags.err_gap = true;
	}

	memcpy((void*)dd, (void*)&data, sizeof(display_data_t));
}

static void IRAM_ATTR data_rx_timeout_dis_1(void *arg)
{
	const size_t buff_size = rx_index_display_1;
	rx_index_display_1 = 0;
	if (buff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_1, data1_display_1, DATA_SET_SIZE*sizeof(tu_2byte_t));
	// memcpy(process_data2_display_1, data2_display_1, DATA_SET_SIZE);

	// memset((void *)&(display_data->display_1.display_data), 0, sizeof(display_data_t));
	get_display_data(&display_data->display_1.display_data, process_data1_display_1);
	display_data->got_display_1 = true;
}
static void IRAM_ATTR data_rx_timeout_dis_2(void *arg)
{
	const uint32_t biff_size = rx_index_display_2;
	rx_index_display_2 = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_2, data1_display_2, DATA_SET_SIZE*sizeof(tu_2byte_t));
	// memcpy(process_data2_display_2, data2_display_2, DATA_SET_SIZE);

	// memset((void *)&(display_data->display_2.display_data), 0, sizeof(display_data_t));
	get_display_data(&(display_data->display_2.display_data), process_data1_display_2);
	display_data->got_display_2 = true;
}

void display_censtar_7_digit_init(display_t *dis)
{
    display_data = dis;

    pinMode(DIS_ENB, OUTPUT);
	pinMode(D1_SCLK, INPUT);
	pinMode(D1_RCLK, INPUT);
	pinMode(D1_SDATA1, INPUT);
	pinMode(D2_SCLK, INPUT);
	pinMode(D2_RCLK, INPUT);
	pinMode(D2_SDATA1, INPUT);

	pinMode(D1_SDATA2, INPUT_PULLUP);
	pinMode(D2_SDATA2, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(D1_SCLK), bit_read_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D1_RCLK), read_end_12bit_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_SCLK), bit_read_dis_2, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_RCLK), read_end_12bit_dis_2, RISING);

	os_timer_disarm(&ptimer_dis_1);
	os_timer_setfn(&ptimer_dis_1, (os_timer_func_t *)data_rx_timeout_dis_1, NULL);
	os_timer_disarm(&ptimer_dis_2);
	os_timer_setfn(&ptimer_dis_2, (os_timer_func_t *)data_rx_timeout_dis_2, NULL);

    digitalWrite(DIS_ENB, true);
}
