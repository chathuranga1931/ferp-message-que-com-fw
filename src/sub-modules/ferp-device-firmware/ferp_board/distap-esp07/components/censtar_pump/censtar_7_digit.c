#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "settings.h"
#include "censtar_7_digit.h"

// convert price into display counts
#define PRICE_TO_DIS_COUNTS(x) (x * 100) // in Censtar 7 digit, two decimal point is showing.
#define VOLUME_COUNTS 1000ULL              // in Censtar 6 digit, have three decimal digits

#define DATA_SET_SIZE 19

#define DIS_ENB GPIO_NUM_15

#define D1_SCLK GPIO_NUM_14
#define D1_RCLK GPIO_NUM_12
#define D1_SDATA1 GPIO_NUM_13

#define D2_SCLK GPIO_NUM_4
#define D2_RCLK GPIO_NUM_5
#define D2_SDATA1 GPIO_NUM_2

#define D1_SDATA2 GPIO_NUM_16 // not used
#define D2_SDATA2 GPIO_NUM_0  // not used

#define c_pin_sdata1_read_dis_1 (bool)((GPIO.in & BIT(D1_SDATA1))) //((GPIO.in >> D1_SDATA1) & 0x1) // GPIO_INPUT_GET(GPIO_ID_PIN(13))		  // pin number 13

#define c_pin_sdata1_read_dis_2 (bool)((GPIO.in & BIT(D2_SDATA1))) //((GPIO.in >> D2_SDATA1) & 0x1) // GPIO_INPUT_GET(GPIO_ID_PIN(2)) // pin number 2

/**
 * Arduino simulator pinout
 * SDATA1 3
 * SCLK 6
 * RCLK 4
 * SEND_START 13
 */

typedef enum
{
	DIS_CHARACTOR_0 = 0,
	DIS_CHARACTOR_1,
	DIS_CHARACTOR_2,
	DIS_CHARACTOR_3,
	DIS_CHARACTOR_4,
	DIS_CHARACTOR_5,
	DIS_CHARACTOR_6,
	DIS_CHARACTOR_7,
	DIS_CHARACTOR_8,
	DIS_CHARACTOR_9,
	DIS_CHARACTOR_L,
	DIS_CHARACTOR_H,
	DIS_CHARACTOR_P,
	DIS_CHARACTOR_A,
	DIS_CHARACTOR_DASH,
	DIS_CHARACTOR_BLANK,
} display_charactor_t;

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

	IDX_SELECT_L = IDX_TOTAL_6,
	IDX_SELECT_P = IDX_SELECT_L
} byte_index_t;

typedef enum
{
	LL_IDX_LL_1 = IDX_TOTAL_5,
	LL_IDX_LL_2 = IDX_TOTAL_6,

	LL_IDX_TOT_LITERS_0  = IDX_VOLUME_0,
	LL_IDX_TOT_LITERS_1  = IDX_VOLUME_1,
	LL_IDX_TOT_LITERS_2  = IDX_VOLUME_2,
	LL_IDX_TOT_LITERS_3  = IDX_VOLUME_3,
	LL_IDX_TOT_LITERS_4  = IDX_VOLUME_4,
	LL_IDX_TOT_LITERS_5  = IDX_VOLUME_5,
	LL_IDX_TOT_LITERS_6  = IDX_VOLUME_6,
	LL_IDX_TOT_LITERS_7  = IDX_TOTAL_0,
	LL_IDX_TOT_LITERS_8  = IDX_TOTAL_1,
	LL_IDX_TOT_LITERS_9  = IDX_TOTAL_2,
	LL_IDX_TOT_LITERS_10 = IDX_TOTAL_3,
	LL_IDX_TOT_LITERS_11 = IDX_TOTAL_4,
} ll_byte_intex_t;

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
	// struct
	// {
	// 	uint16_t LSB : 4; // first 4 bits, IDX 16 ~ 31
	// 	uint16_t MDB : 4; // mid 4 bits, IDX 0 ~ 15
	// 	uint16_t MSB : 4; // last 4 bits
	// 	uint16_t : 4;	  // rest 4 bits
	// };
	tuByte_t tu8int[2];
	uint16_t u16int;
} tu_word_t;

typedef struct
{
	uint8_t id;
	tu_word_t ab[DATA_SET_SIZE];
} capture_data_t;

static xQueueHandle capture_queue = NULL;
static xQueueHandle *ptr_send_que = NULL;

// display tap 1
static volatile bool start_display_1 = false;
static volatile uint16_t bits1_display_1 = 0;
static capture_data_t capture_display_1 = {.id = TX_ID_DIS1_DATA};

// display tap 2
static volatile bool start_display_2 = false;
static volatile uint16_t bits1_display_2 = 0;
static capture_data_t capture_display_2 = {.id = TX_ID_DIS2_DATA};

static void pin_interrupt_enable_dis_1();
static void pin_interrupt_enable_dis_2();
static void pin_interrupt_disable_dis_1();
static void pin_interrupt_disable_dis_2();

static void IRAM_ATTR bit_read_dis_1(void *arg)
{
    bits1_display_1 = (uint16_t)((uint16_t)(c_pin_sdata1_read_dis_1) | (uint16_t)(bits1_display_1 << 1));
}

static void IRAM_ATTR bits_read_end_dis_1(void *arg)
{
	const uint8_t index1 =  (uint8_t)((bits1_display_1 & 0xF0) >> 4);
	const uint8_t index2 =  (bits1_display_1 & 0x0F);

	switch (index1)
	{
	case 0x0:
		start_display_1 = true;
		__attribute__((fallthrough));  // Explicit fallthrough
	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
	case 0x8:
	case 0x9:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
		if(start_display_1)
		{
			capture_display_1.ab[index1].u16int = bits1_display_1;
		}
		break;
	case 0xF:
		switch (index2)
		{
		case 0x0:
		case 0x1:
			if(start_display_1)
			{
				capture_display_1.ab[index2 + 16].u16int = bits1_display_1;
			}
			break;
		case 0x2:
			if(start_display_1)
			{
				capture_display_1.ab[index2 + 16].u16int = bits1_display_1;
				pin_interrupt_disable_dis_1();
				xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
			}
			break;
		default:
			if(start_display_1)
			{
				capture_display_1.ab[index1].u16int = bits1_display_1;
			}
			break;
		}
		break;
	
	default:
		break;
	}

	//clear bit memory
	bits1_display_1 = 0;
}

static void IRAM_ATTR bit_read_dis_2(void *arg)
{
    bits1_display_2 = (uint16_t)((uint16_t)(c_pin_sdata1_read_dis_2) | (uint16_t)(bits1_display_2 << 1));
}

static void IRAM_ATTR bits_read_end_dis_2(void *arg)
{
	const uint8_t index1 =  (uint8_t)((bits1_display_2 & 0xF0) >> 4);
	const uint8_t index2 =  (bits1_display_2 & 0x0F);

	switch (index1)
	{
	case 0x0:
		start_display_2 = true;
		__attribute__((fallthrough));  // Explicit fallthrough
	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
	case 0x8:
	case 0x9:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
		if(start_display_2)
		{
			capture_display_2.ab[index1].u16int = bits1_display_2;
		}
		break;
	case 0xF:
		switch (index2)
		{
		case 0x0:
		case 0x1:
			if(start_display_2)
			{
				capture_display_2.ab[index2 + 16].u16int = bits1_display_2;
			}
			break;
		case 0x2:
			if(start_display_2)
			{
				capture_display_2.ab[index2 + 16].u16int = bits1_display_2;
				pin_interrupt_disable_dis_2();
				xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
			}
			break;
		default:
			if(start_display_2)
			{
				capture_display_2.ab[index1].u16int = bits1_display_2;
			}
			break;
		}
		break;
	
	default:
		break;
	}

	//clear bit memory
	bits1_display_2 = 0;
}


/* 356.00 586083   16463
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
05A5    0000 0101 1010 0101  0   5   A   5
0FB5    0000 1111 1011 0101  0   F   B   5
03C5    0000 0011 1100 0101  0   3   C   5
06D5    0000 0110 1101 0101  0   6   D   5
04E5    0000 0100 1110 0101  0   4   E   5
06F3    0000 0110 1111 0011  0   6   F   3
01F0    0000 0001 1111 0000  0   1   F   0
0FF1    0000 1111 1111 0001  0   F   F   1
0FF2    0000 1111 1111 0010  0   F   F   2

* Packet has two 19 byte arrays.
* MDB Mid  (4 bits) of the 12bit represents byte index. If index is not matching, make error relevant to parameter
* MSB Left (4 bits) represent the decimal number for display.
* LSB Right (4 bits) represents the index after exeeding Mid 4 bytes.
*	- Value 0 to 9 represents decimal 0 to 9.
*	- 10 -> L
*	- 11 -> H
*	- 12 -> P
*	- 13 -> A
*	- 14 -> -
*	- 15 -> Blank
*
*/
static void get_display_data(cens_7_digit_t *dd, tu_word_t *ab)
{
	cens_7_digit_t data = {};

	/*Check user enters Total Liter cosumption display*/
	data.flags.select_ll = (bool)(ab[LL_IDX_LL_1].tu8int[1].LS == DIS_CHARACTOR_L && ab[LL_IDX_LL_2].tu8int[1].LS == DIS_CHARACTOR_L);
	data.flags.select_l = (bool)(ab[IDX_SELECT_L].tu8int[1].LS == DIS_CHARACTOR_L);
	data.flags.select_p = (bool)(ab[IDX_SELECT_P].tu8int[1].LS == DIS_CHARACTOR_P);

	for (size_t i = 0; i < DATA_SET_SIZE; i++)
	{
		switch (i)
		{
		case IDX_UNIT_0:
		case IDX_UNIT_1:
		case IDX_UNIT_2:
		case IDX_UNIT_3:
		case IDX_UNIT_4:
			if(ab[i].tu8int[0].MS != i)
			{
				data.error.err_bit.unitprice = true;
                data.error.err_bit.index = true;
			}
			break;
		case IDX_TOTAL_0:
		case IDX_TOTAL_1:
		case IDX_TOTAL_2:
		case IDX_TOTAL_3:
		case IDX_TOTAL_4:
		case IDX_TOTAL_5:
		case IDX_TOTAL_6:
			if(ab[i].tu8int[0].MS != i)
			{
				data.error.err_bit.totprice = true;
				data.error.err_bit.index = true;
			}
			break;
		case IDX_VOLUME_0:
		case IDX_VOLUME_1:
		case IDX_VOLUME_2:
		case IDX_VOLUME_3:
			if(ab[i].tu8int[0].MS != i)
			{
				data.error.err_bit.volume = true;
				data.error.err_bit.index = true;
			}
			break;
		case IDX_VOLUME_4:
		case IDX_VOLUME_5:
		case IDX_VOLUME_6:
			if((ab[i].tu8int[0].MS != 0xF) || (ab[i].tu8int[0].LS != (i - 16)))
			{
				data.error.err_bit.volume = true;
				data.error.err_bit.index = true;
			}
			break;
		default:
			break;
		}

		if (ab[i].tu8int[1].LS > 9){
			ab[i].tu8int[1].LS = 0;
		}
	}

	data.unit_price = (uint32_t)(ab[IDX_UNIT_4].tu8int[1].LS) * 10000 +
					  (uint32_t)(ab[IDX_UNIT_3].tu8int[1].LS) * 1000 +
					  (uint32_t)(ab[IDX_UNIT_2].tu8int[1].LS) * 100 +
					  (uint32_t)(ab[IDX_UNIT_1].tu8int[1].LS) * 10 +
					  (uint32_t)(ab[IDX_UNIT_0].tu8int[1].LS);

	/*if Total Liter option is selected, calculate it*/
	if (data.flags.select_ll)
	{
		data.total_liters = (uint64_t)(ab[LL_IDX_TOT_LITERS_11].tu8int[1].LS) * 100000000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_10].tu8int[1].LS) * 10000000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_9].tu8int[1].LS)  * 1000000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_8].tu8int[1].LS)  * 100000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_7].tu8int[1].LS)  * 10000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_6].tu8int[1].LS)  * 1000000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_5].tu8int[1].LS)  * 100000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_4].tu8int[1].LS)  * 10000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_3].tu8int[1].LS)  * 1000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_2].tu8int[1].LS)  * 100 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_1].tu8int[1].LS)  * 10 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_0].tu8int[1].LS);
	}
	else
	{

		data.total_price = (uint32_t)(ab[IDX_TOTAL_6].tu8int[1].LS) * 1000000 +
						   (uint32_t)(ab[IDX_TOTAL_5].tu8int[1].LS) * 100000 +
						   (uint32_t)(ab[IDX_TOTAL_4].tu8int[1].LS) * 10000 +
						   (uint32_t)(ab[IDX_TOTAL_3].tu8int[1].LS) * 1000 +
						   (uint32_t)(ab[IDX_TOTAL_2].tu8int[1].LS) * 100 +
						   (uint32_t)(ab[IDX_TOTAL_1].tu8int[1].LS) * 10 +
						   (uint32_t)(ab[IDX_TOTAL_0].tu8int[1].LS);

		data.volume_l = (uint32_t)(ab[IDX_VOLUME_6].tu8int[1].LS) * 1000000 +
						(uint32_t)(ab[IDX_VOLUME_5].tu8int[1].LS) * 100000 +
						(uint32_t)(ab[IDX_VOLUME_4].tu8int[1].LS) * 10000 +
						(uint32_t)(ab[IDX_VOLUME_3].tu8int[1].LS) * 1000 +
						(uint32_t)(ab[IDX_VOLUME_2].tu8int[1].LS) * 100 +
						(uint32_t)(ab[IDX_VOLUME_1].tu8int[1].LS) * 10 +
						(uint32_t)(ab[IDX_VOLUME_0].tu8int[1].LS);
		
		//decimal point of total price and unit price is equal, so consider volume decimal points only
		uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
		gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
		if (gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR)) // if total price should match with unit_price*valume. Check for the tolerance
		{
			data.error.err_bit.price_gap = true;
		}
	}

	memcpy((void *)dd, (void *)&data, sizeof(cens_7_digit_t));
}

static void data_send_task(void *arg)
{
	TickType_t ticks_last[TX_ID_DIS_DATA_SIZE] = { xTaskGetTickCount(), xTaskGetTickCount() };
	capture_data_t capture_now = {};
    capture_data_t capture_dis[TX_ID_DIS_DATA_SIZE] = {};
	data_packet_t display_data = {
		.display = DIS_CENSTAR_7_DIGIT,
		.length = sizeof(cens_7_digit_t)};

	while (1)
	{
		if (xQueueReceive(capture_queue, &capture_now, pdMS_TO_TICKS(10) /*portMAX_DELAY*/))
		{
            const size_t dis_id = capture_now.id;
			// if received data is different from what we have and it is within time range for send
            const TickType_t ticks_now = xTaskGetTickCount();
			// send data on timeout or mismatch with previous one
			if (ticks_now - ticks_last[dis_id] > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(capture_dis[dis_id].ab, capture_now.ab, DATA_SET_SIZE))
			{
				const capture_data_t now_temp = capture_now;
				ticks_last[dis_id] = ticks_now;
                display_data.pck_id = dis_id;
				get_display_data((cens_7_digit_t *)(display_data.ab_data), capture_now.ab);
				if((((cens_7_digit_t*)(display_data.ab_data))->error.u8int & settings.error_mask.u8int) == 0) //if retain errors are none zero, do not send
				{
					memcpy(capture_dis[dis_id].ab, now_temp.ab, sizeof(now_temp.ab)); //copy correct packet for next comparison
					xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10)); // if queue is full, wait 10ms until it gets clear
				}
                // const cens_7_digit_t *dis = (cens_7_digit_t *)(display_data.ab_data);
                // printf("dis=%d\tunit=%d\ttotal=%d\tvolume=%d\terr=%d\r\n\r\n", capture_now.id, dis->unit_price, dis->total_price, dis->volume_l, dis->error.u8int);

			}
		// end:
            // enable the display interrupt according to display ID
            switch (dis_id)
            {
            case TX_ID_DIS1_DATA:
                pin_interrupt_enable_dis_1();
                break;
            case TX_ID_DIS2_DATA:
                pin_interrupt_enable_dis_2();
                break;
            default:
                break;
            }
		}
		if ((xTaskGetTickCount() - ticks_last[TX_ID_DIS1_DATA]) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
			capture_data_t temp = capture_dis[TX_ID_DIS1_DATA];
            ticks_last[TX_ID_DIS1_DATA] = xTaskGetTickCount();
            display_data.pck_id = TX_ID_DIS1_DATA;
            get_display_data((cens_7_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        if ((xTaskGetTickCount() - ticks_last[TX_ID_DIS2_DATA]) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
			capture_data_t temp = capture_dis[TX_ID_DIS2_DATA];
            ticks_last[TX_ID_DIS2_DATA] = xTaskGetTickCount();
            display_data.pck_id = TX_ID_DIS2_DATA;
            get_display_data((cens_7_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
	}
	vTaskDelete(NULL);
}

static void pin_interrupt_enable_dis_1()
{
    start_display_1 = false;
    memset(capture_display_1.ab, 0, sizeof(capture_display_1.ab));
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_enable_dis_2()
{
    start_display_2 = false;
    memset(capture_display_2.ab, 0, sizeof(capture_display_1.ab));
    gpio_set_intr_type(D2_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D2_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_disable_dis_1()
{
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_DISABLE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_DISABLE);
}
static void pin_interrupt_disable_dis_2()
{
    gpio_set_intr_type(D2_SCLK, GPIO_INTR_DISABLE);
    gpio_set_intr_type(D2_RCLK, GPIO_INTR_DISABLE);
}

esp_err_t display_censtar_7_digit_init(xQueueHandle *send_que)
{
	gpio_config_t io_conf;

	// set display enable pin output and turn off
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = BIT(DIS_ENB);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);

	// Set two clock positive edge intterupt pin inputs
	io_conf.intr_type = GPIO_INTR_POSEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT(D1_SCLK) | BIT(D1_RCLK) | BIT(D2_SCLK) | BIT(D2_RCLK);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);

	// Set DATA1 bits reading inputs
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT(D1_SDATA1) | BIT(D2_SDATA1);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);

	// Set DATA2 pins as pull ups because no use
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT(D1_SDATA2) | BIT(D2_SDATA2);
	io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);

	// install gpio isr service
	gpio_install_isr_service(0);
	// hook isr handler for specific gpio pin
	gpio_isr_handler_add(D1_SCLK, bit_read_dis_1, NULL);
	gpio_isr_handler_add(D1_RCLK, bits_read_end_dis_1, NULL);
	gpio_isr_handler_add(D2_SCLK, bit_read_dis_2, NULL);
	gpio_isr_handler_add(D2_RCLK, bits_read_end_dis_2, NULL);

	capture_queue = xQueueCreate(20, sizeof(capture_data_t));
	if (capture_queue == NULL)
	{
		return ESP_FAIL;
	}

	ptr_send_que = send_que;
    bits1_display_1 = 0;
    bits1_display_2 = 0;

	if (xTaskCreate(data_send_task, "data_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
	{
		return ESP_FAIL;
	}
	gpio_set_level(DIS_ENB, true);
	// printf("Staring Censtar 7 display\r\n");
	return ESP_OK;
}