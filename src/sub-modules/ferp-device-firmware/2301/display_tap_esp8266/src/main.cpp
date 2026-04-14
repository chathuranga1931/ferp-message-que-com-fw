#include "Arduino.h"
// #include <fast_io.h>
#include <gpio.h>
#include <ESP8266WiFi.h>

#include "gpio.h"
// #include "gpio16.h"
// #define DR_REG_GPIO_BASE                        0x3ff44000
// #define GPIO_IN_REG          (DR_REG_GPIO_BASE + 0x003c)
// #define GP_FAST_READ(pin, reg) ((*(const volatile uint32_t*)(reg) >> ((pin)&31)) & 1)

#define GP_IN(pin) GPIO_INPUT_GET(GPIO_ID_PIN(pin)) // GP_FAST_READ(pin, GPIO_IN_REG)
#define RX_BUF_SIZE 30
#define DATA_SET_SIZE 14

#define c_pin_sclk 12
#define c_pin_rclk 13
#define c_pin_sdata1 14
#define c_pin_sdata2 16
#define c_pin_sdata1_read GPIO_INPUT_GET(GPIO_ID_PIN(14))
#define c_pin_sdata2_read (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1)

typedef union
{
	struct 
	{
		uint8_t LS : 4; // first 4 bits
		uint8_t MS : 4; // last 4 bits 
	};
    uint8_t u8int;
}tuByte_t;
// typedef union //__attribute__((packed))
// {
//     struct
//     {
//         uint8_t bit0 : 1;
//         uint8_t bit1 : 1;
//         uint8_t bit2 : 1;
//         uint8_t bit3 : 1;
//         uint8_t bit4 : 1;
//         uint8_t bit5 : 1;
//         uint8_t bit6 : 1;
//         uint8_t bit7 : 1;
//     };
//     uint8_t u8int;
//     int8_t s8int;
//     uint8_t ab;
// } tuByte_t;


typedef struct
{
	uint32_t unit_price;  //1 * 0.01 price
	uint32_t total_price; //1 * 0.01 price
	uint32_t volume_l;    //1 * 0.001 volume
	bool start_stop;
	bool select_p;
	bool select_l;
} display_data_t;

uint8_t bitcout = 0;
uint8_t byte1 = 0;
uint8_t byte2 = 0;
uint8_t rx1_buffer[RX_BUF_SIZE] = {};
uint8_t rx2_buffer[RX_BUF_SIZE] = {};
tuByte_t process_rx1_buffer[RX_BUF_SIZE] = {};
tuByte_t process_rx2_buffer[RX_BUF_SIZE] = {};
uint32_t rx_buf_index = 0;
display_data_t display_data_1;

static os_timer_t ptimer;

void IRAM_ATTR bit_read(void) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();

	// Serial.write(48 + c_pin_sdata1_read);

	// var1 = (uint8_t)(var1 | (uint8_t)(c_pin_sdata1_read<<bitcout));
	// bitcout--;

	byte1 = (uint8_t)((uint8_t)(c_pin_sdata1_read) | (uint8_t)(byte1 << 1));
	byte2 = (uint8_t)((uint8_t)(c_pin_sdata2_read) | (uint8_t)(byte2 << 1));

	// Serial.println(var1);
	ETS_GPIO_INTR_ENABLE();
}

void IRAM_ATTR byte_read_start(void) // ICACHE_RAM_ATTR
{
	ETS_GPIO_INTR_DISABLE();
	rx1_buffer[rx_buf_index] = byte1;
	rx2_buffer[rx_buf_index] = byte2;

	rx_buf_index++;
	if (!(rx_buf_index < RX_BUF_SIZE))
		rx_buf_index = 0;

	os_timer_arm(&ptimer, 20, 0);
	ETS_GPIO_INTR_ENABLE();
}

void get_display_data(display_data_t *dd, tuByte_t *ba, tuByte_t *bb)
{

	for (int i = 0; i < 14; i++)
	{
		if(ba[i].MS >= 10)
			ba[i].u8int = 0;
		if(bb[i].MS >= 10)
			bb[i].u8int = 0;
		
		// if ((ba[i] >> 4) >= 10)
		// 	ba[i] = 0;
		// if ((bb[i] >> 4) >= 10)
		// 	bb[i] = 0;
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
		dd->start_stop = true;
	}
	else
	{
		dd->start_stop = false;
	}

	if ((uint8_t)(ba[0].MS) == 10)
	{
		dd->select_l = true;
	}
	else
	{
		dd->select_l = false;
	}

	if ((uint8_t)(bb[0].MS) == 12)
	{
		dd->select_p = true;
	}
	else
	{
		dd->select_p = false;
	}
}
// void get_display_data(display_data_t *dd, uint8_t *ba, uint8_t *bb)
// {
// 	for (int i = 0; i < 14; i++)
// 	{
// 		if ((ba[i] >> 4) >= 10)
// 			ba[i] = 0;
// 		if ((bb[i] >> 4) >= 10)
// 			bb[i] = 0;
// 	}
// 	dd->unit_price = (ba[8] >> 4) * 1000.0 +
// 					 (ba[9] >> 4) * 100.0 +
// 					 (ba[10] >> 4) * 10.0 +
// 					 (ba[11] >> 4) * 1.0 +
// 					 (ba[12] >> 4) * 0.1 +
// 					 (ba[13] >> 4) * 0.01;
// 	dd->total_price = (ba[1] >> 4) * 10000.0 +
// 					  (ba[2] >> 4) * 1000.0 +
// 					  (ba[3] >> 4) * 100.0 +
// 					  (ba[4] >> 4) * 10.0 +
// 					  (ba[5] >> 4) * 1.0 +
// 					  (ba[6] >> 4) * 0.1 +
// 					  (ba[7] >> 4) * 0.01;
// 	dd->volume_l = (bb[1] >> 4) * 1000.0 +
// 				   (bb[2] >> 4) * 100.0 +
// 				   (bb[3] >> 4) * 10.0 +
// 				   (bb[4] >> 4) * 1.0 +
// 				   (bb[5] >> 4) * 0.1 +
// 				   (bb[6] >> 4) * 0.01 +
// 				   (bb[7] >> 4) * 0.001
// 	if ((bb[8] >> 4) == 1)
// 	{
// 		dd->start_stop = true;
// 	}
// 	else
// 	{
// 		dd->start_stop = false;
// 	}
// 	if ((ba[0] >> 4) == 10)
// 	{
// 		dd->select_l = true;
// 	}
// 	else
// 	{
// 		dd->select_l = false;
// 	}
// 	if ((bb[0] >> 4) == 12)
// 	{
// 		dd->select_p = true;
// 	}
// 	else
// 	{
// 		dd->select_p = false;
// 	}
// }

void IRAM_ATTR data_rx_timeout(void *arg)
{
	const uint32_t biff_size = rx_buf_index;
	rx_buf_index = 0;
	if (biff_size < DATA_SET_SIZE)
		return;

	memcpy(process_rx1_buffer, rx1_buffer, DATA_SET_SIZE);
	memcpy(process_rx2_buffer, rx2_buffer, DATA_SET_SIZE);

	// for (uint32_t i = 0; i < DATA_SET_SIZE; i++)
	// {
	// 	Serial.print(process_rx1_buffer[i].u8int, HEX);
	// 	Serial.print(',');
	// }
	// Serial.println();
	// for (uint32_t i = 0; i < DATA_SET_SIZE; i++)
	// {
	// 	Serial.print(process_rx2_buffer[i].u8int, HEX);
	// 	Serial.print(',');
	// }
	// Serial.print("\r\n");

	get_display_data(&display_data_1, process_rx1_buffer, process_rx2_buffer);
	double total_price_expected = display_data_1.unit_price/100.0 * display_data_1.volume_l/1000.0;
	double gap = total_price_expected - display_data_1.total_price/100.0;
	// double modulo_gap = gap < 0 ? gap * (-1) : gap;
	Serial.println("Gap=" + String(gap));
	// if (modulo_gap > 10)
	// {
	// 	Serial.println("Total mismatch detected....");
	// 	return;
	// }

	Serial.println("unit=" + String(display_data_1.unit_price/100.0) + " total=" + String(display_data_1.total_price/100.0) + " volume=" + String(display_data_1.volume_l/1000.0) + " " + (display_data_1.start_stop? "start":"stop") + " select_p=" + (display_data_1.select_p? "true":"false") + " select_l=" + (display_data_1.select_l? "true":"false"));


	// Serial.println("size=" + String(biff_size) + "\r\n");
	Serial.println();
}

void setup()
{
	WiFi.mode(WIFI_OFF);
	pinMode(c_pin_sdata1, INPUT_PULLUP);
	pinMode(c_pin_sdata2, INPUT_PULLUP);
	pinMode(c_pin_sclk, INPUT_PULLUP);
	pinMode(c_pin_rclk, INPUT_PULLUP);

	Serial.begin(115200);
	Serial.println("\r\n");
	Serial.println("Starting display tap....");
	attachInterrupt(digitalPinToInterrupt(c_pin_sclk), bit_read, RISING);
	attachInterrupt(digitalPinToInterrupt(c_pin_rclk), byte_read_start, RISING);

	os_timer_disarm(&ptimer);
	os_timer_setfn(&ptimer, (os_timer_func_t *)data_rx_timeout, NULL);
}

void loop()
{
	// testbyte = testbyte<<1;

	// testbyte = (uint8_t)((uint8_t)c_pin_sdata1_read | (uint8_t)(testbyte<<1));
	// Serial.print(String((uint8_t)c_pin_sdata1_read) + ", ");
	// Serial.print(testbyte, BIN);
	//   const int32_t biff_size = rx_buf_index;

	//   for(int i = 0; i<biff_size; i++){
	//     Serial.print(rx1_buffer[i]);
	//     Serial.print(',');
	//   }
	//   Serial.println();
	//     for(int i = 0; i<biff_size; i++){
	//     Serial.print(rx2_buffer[i]);
	//     Serial.print(',');
	//   }
	//   Serial.println("\r\n");
	//   delay(2000);
}
// ICACHE_FLASH_ATTR
