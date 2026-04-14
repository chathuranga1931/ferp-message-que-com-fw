#include "Arduino.h"
#include <ESP8266WiFi.h>

#define SW_MAJOR 1
#define SW_MINOR 2

#define GP_IN(pin) GPIO_INPUT_GET(GPIO_ID_PIN(pin)) // GP_FAST_READ(pin, GPIO_IN_REG)
#define RX_BUF_SIZE 30
#define DATA_SET_SIZE 14

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

typedef union __attribute__((packed))
{
	struct
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t rest : 5;
	};
	uint8_t byte;
} flags_t;

typedef struct __attribute__((packed))
{
	flags_t flags;
	uint32_t unit_price;  // 1 * 0.01 price
	uint32_t total_price; // 1 * 0.01 price
	uint32_t volume_l;	  // 1 * 0.001 volume
} display_data_t;

typedef struct __attribute__((packed))
{
	uint8_t sw_major;
	uint8_t sw_minor;
	uint8_t display_id;
	display_data_t display_data;
	uint32_t checksum;
} packet_data_t;

const uint32_t packet_data_size = sizeof(packet_data_t);
const uint32_t packet_data_lenght = (sizeof(packet_data_t) - sizeof(packet_data_t::checksum)); // seize of whole packet - size of checksum

static os_timer_t ptimer_dis_1;
static os_timer_t ptimer_dis_2;

// display tap 1
uint8_t byte1_display_1 = 0, byte2_display_1 = 0;
uint8_t data1_display_1[RX_BUF_SIZE] = {};
uint8_t data2_display_1[RX_BUF_SIZE] = {};
tuByte_t process_data1_display_1[RX_BUF_SIZE] = {};
tuByte_t process_data2_display_1[RX_BUF_SIZE] = {};
uint32_t rx_index_display_1 = 0;
display_data_t display_data_1 = {};
bool got_display_1 = false;
// display tap 2
uint8_t byte1_display_2 = 0, byte2_display_2 = 0;
uint8_t data1_display_2[RX_BUF_SIZE] = {};
uint8_t data2_display_2[RX_BUF_SIZE] = {};
tuByte_t process_data1_display_2[RX_BUF_SIZE] = {};
tuByte_t process_data2_display_2[RX_BUF_SIZE] = {};
uint32_t rx_index_display_2 = 0;
display_data_t display_data_2 = {};
bool got_display_2 = false;

packet_data_t send_data_1 = {}, send_data_2 = {}, dummy_data = {};
unsigned long time_now, time_prev;

void IRAM_ATTR bit_read_dis_1(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_1 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_1) | (uint8_t)(byte1_display_1 << 1));
	byte2_display_1 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_1) | (uint8_t)(byte2_display_1 << 1));

	ETS_GPIO_INTR_ENABLE();
}

void IRAM_ATTR byte_read_start_dis_1(void) // ICACHE_RAM_ATTR
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

void IRAM_ATTR bit_read_dis_2(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	byte1_display_2 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_2) | (uint8_t)(byte1_display_2 << 1));
	byte2_display_2 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_2) | (uint8_t)(byte2_display_2 << 1));

	ETS_GPIO_INTR_ENABLE();
}

void IRAM_ATTR byte_read_start_dis_2(void) // ICACHE_RAM_ATTR
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

void get_display_data(display_data_t *dd, tuByte_t *ba, tuByte_t *bb)
{

	for (int i = 0; i < 14; i++)
	{
		if (ba[i].MS >= 10)
			ba[i].u8int = 0;
		if (bb[i].MS >= 10)
			bb[i].u8int = 0;
	}

	dd->unit_price = (uint32_t)(ba[8].MS) * 100000 +
					 (uint32_t)(ba[9].MS) * 10000 +
					 (uint32_t)(ba[10].MS) * 1000 +
					 (uint32_t)(ba[11].MS) * 100 +
					 (uint32_t)(ba[12].MS) * 10 +
					 (uint32_t)(ba[13].MS);

	dd->total_price = (uint32_t)(ba[1].MS) * 1000000 +
					  (uint32_t)(ba[2].MS) * 100000 +
					  (uint32_t)(ba[3].MS) * 10000 +
					  (uint32_t)(ba[4].MS) * 1000 +
					  (uint32_t)(ba[5].MS) * 100 +
					  (uint32_t)(ba[6].MS) * 10 +
					  (uint32_t)(ba[7].MS);

	dd->volume_l = (uint32_t)(bb[1].MS) * 1000000 +
				   (uint32_t)(bb[2].MS) * 100000 +
				   (uint32_t)(bb[3].MS) * 10000 +
				   (uint32_t)(bb[4].MS) * 1000 +
				   (uint32_t)(bb[5].MS) * 100 +
				   (uint32_t)(bb[6].MS) * 10 +
				   (uint32_t)(bb[7].MS);

	if ((uint8_t)(bb[8].MS) == 1)
	{
		dd->flags.start_stop = true;
	}
	else
	{
		dd->flags.start_stop = false;
	}

	if ((uint8_t)(ba[0].MS) == 10)
	{
		dd->flags.select_l = true;
	}
	else
	{
		dd->flags.select_l = false;
	}

	if ((uint8_t)(bb[0].MS) == 12)
	{
		dd->flags.select_p = true;
	}
	else
	{
		dd->flags.select_p = false;
	}
}

void IRAM_ATTR data_rx_timeout_dis_1(void *arg)
{
	const uint32_t biff_size = rx_index_display_1;
	rx_index_display_1 = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_1, data1_display_1, DATA_SET_SIZE);
	memcpy(process_data2_display_1, data2_display_1, DATA_SET_SIZE);

	memset((void *)&display_data_1, 0, sizeof(display_data_t));

	get_display_data(&display_data_1, process_data1_display_1, process_data2_display_1);
	// double total_price_expected = display_data_1.unit_price / 100.0 * display_data_1.volume_l / 1000.0;
	// double gap = total_price_expected - display_data_1.total_price / 100.0;
	// double modulo_gap = gap < 0 ? gap * (-1) : gap;

	got_display_1 = true;

	// Serial.println("Gap=" + String(gap));
	// Serial.println("unit=" + String(display_data_1.unit_price / 100.0) + " total=" + String(display_data_1.total_price / 100.0) + " volume=" + String(display_data_1.volume_l / 1000.0) + " " + (display_data_1.start_stop ? "start" : "stop") + " select_p=" + (display_data_1.select_p ? "true" : "false") + " select_l=" + (display_data_1.select_l ? "true" : "false"));
	// Serial.println();
}
void IRAM_ATTR data_rx_timeout_dis_2(void *arg)
{
	const uint32_t biff_size = rx_index_display_2;
	rx_index_display_2 = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_data1_display_2, data1_display_2, DATA_SET_SIZE);
	memcpy(process_data2_display_2, data2_display_2, DATA_SET_SIZE);

	memset((void *)&display_data_2, 0, sizeof(display_data_t));
	get_display_data(&display_data_2, process_data1_display_2, process_data2_display_2);

	got_display_2 = true;
}
/*
 * 0xff,0xff,0x4e,0x0b,0x10,0x15,0xfe,0x00,0x30,0xfe,0x01,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
 * 0xff,0xff,0x4e,0x0b,0x10,0x15,  0xff   ,0x30,  0xfe   ,0x40,0x02,0x00,0x00,0x00,0x80,0x64,0xb4,0xff
 */
void send_packet(packet_data_t *pckt)
{
	Serial.write(0xFF);
	Serial.write(0xFF);
	for (size_t i = 0; i < packet_data_size; i++)
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

void send_data_display_1()
{
	memcpy((void *)&send_data_1.display_data, (void *)&display_data_1, sizeof(display_data_t));
	send_data_1.sw_major = SW_MAJOR;
	send_data_1.sw_minor = SW_MINOR;
	send_data_1.display_id = 0;
	send_data_1.checksum = 0;
	for (size_t i = 0; i < packet_data_lenght; i++)
	{
		send_data_1.checksum += ((uint8_t *)&send_data_1)[i];
	}
	// Serial.write((uint8_t *)&send_data_1, packet_data_size);
	send_packet(&send_data_1);
}
void send_data_display_2()
{
	memcpy((void *)&send_data_2.display_data, (void *)&display_data_2, sizeof(display_data_t));
	send_data_2.sw_major = SW_MAJOR;
	send_data_2.sw_minor = SW_MINOR;
	send_data_2.display_id = 1;
	send_data_2.checksum = 0;
	for (size_t i = 0; i < packet_data_lenght; i++)
	{
		send_data_2.checksum += ((uint8_t *)&send_data_2)[i];
	}
	// Serial.write((uint8_t *)&send_data_2, packet_data_size);
	send_packet(&send_data_2);
}

inline void send_data_dummy()
{
	// Serial.write((uint8_t *)&dummy_data, packet_data_size);
	send_packet(&dummy_data);
}

void setup()
{
	WiFi.mode(WIFI_OFF);
	pinMode(D1_SCLK, INPUT);
	pinMode(D1_RCLK, INPUT);
	pinMode(D1_SDATA1, INPUT);
	pinMode(D1_SDATA2, INPUT);
	pinMode(D2_SCLK, INPUT);
	pinMode(D2_RCLK, INPUT);
	pinMode(D2_SDATA1, INPUT);
	pinMode(D2_SDATA2, INPUT);

	Serial.begin(115200);
	Serial.println("\r\n");
	Serial.println("Starting display tap....");
	attachInterrupt(digitalPinToInterrupt(D1_SCLK), bit_read_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D1_RCLK), byte_read_start_dis_1, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_SCLK), bit_read_dis_2, RISING);
	attachInterrupt(digitalPinToInterrupt(D2_RCLK), byte_read_start_dis_2, RISING);

	os_timer_disarm(&ptimer_dis_1);
	os_timer_setfn(&ptimer_dis_1, (os_timer_func_t *)data_rx_timeout_dis_1, NULL);
	os_timer_disarm(&ptimer_dis_2);
	os_timer_setfn(&ptimer_dis_2, (os_timer_func_t *)data_rx_timeout_dis_2, NULL);

	memset((void *)&dummy_data, 0xAA, packet_data_lenght);
	dummy_data.sw_major = SW_MAJOR;
	dummy_data.sw_minor = SW_MINOR;
	dummy_data.display_id = 2;
	dummy_data.checksum = 0;
	for (size_t i = 0; i < packet_data_lenght; i++)
	{
		dummy_data.checksum += ((uint8_t *)&dummy_data)[i];
	}
	send_data_dummy();
	time_prev = millis();
}

void loop()
{
	if (got_display_1)
	{
		send_data_display_1();
		got_display_1 = false;
		time_prev = time_now;
	}
	if (got_display_2)
	{
		send_data_display_2();
		got_display_2 = false;
		time_prev = time_now;
	}

	// sending dummy data to indicate i am alive
	time_now = millis();
	if ((time_now - time_prev) > 5000)
	{
		send_data_dummy();
		time_prev = time_now;
	}
	// delay(1);
}
// ICACHE_FLASH_ATTR
