#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "sys_types.h"
#include "settings.h"
#include "board_io.h"
#include "device.h"
#include "esp_log.h"

// convert price into display counts
#define PRICE_TO_DIS_COUNTS(x) (x * 100) // in Chinese 8 digit, two decimal points is showing.
#define VOLUME_COUNTS 1000ULL			 // in Chinese 8 digit, have three decimal digits

#define DATA_SET_SIZE 14
#define PACKET_TOUT_US 20 * 1000 // if packet not get complete yet, this timeout occurs

#define c_pin_sdata1_read_dis_1 (bool)(gpio_get_level(D1_SDATA1))
#define c_pin_sdata2_read_dis_1 (bool)(gpio_get_level(D1_SDATA2))

#define c_pin_sdata1_read_dis_2 (bool)(gpio_get_level(D2_SDATA1))
#define c_pin_sdata2_read_dis_2 (bool)(gpio_get_level(D2_SDATA2))

/**
 * Arduino simulator pinout
 * SDATA1 3
 * SDATA2 2
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
	IDX_TOTAL_7 = 0,
	IDX_TOTAL_6,
	IDX_TOTAL_5,
	IDX_TOTAL_4,
	IDX_TOTAL_3,
	IDX_TOTAL_2,
	IDX_TOTAL_1,
	IDX_TOTAL_0,

	IDX_UNIT_5 = 8,
	IDX_UNIT_4,
	IDX_UNIT_3,
	IDX_UNIT_2,
	IDX_UNIT_1,
	IDX_UNIT_0,

	IDX_SELECT_L = IDX_TOTAL_7,
} ab_byte_index_t;

typedef enum
{
	IDX_VOLUME_7 = 0,
	IDX_VOLUME_6,
	IDX_VOLUME_5,
	IDX_VOLUME_4,
	IDX_VOLUME_3,
	IDX_VOLUME_2,
	IDX_VOLUME_1,
	IDX_VOLUME_0,

	IDX_SELECT_P = IDX_VOLUME_7,
	IDX_START_STOP = 8,
} bb_byte_index_t;

typedef enum
{
	LL_IDX_LL_1 = IDX_TOTAL_6,
	LL_IDX_LL_2 = IDX_TOTAL_7,

	LL_IDX_TOT_LITERS_0 = IDX_UNIT_0,
	LL_IDX_TOT_LITERS_1 = IDX_UNIT_1,
	LL_IDX_TOT_LITERS_2 = IDX_UNIT_2,
	LL_IDX_TOT_LITERS_3 = IDX_UNIT_3,
	LL_IDX_TOT_LITERS_4 = IDX_UNIT_4,
	LL_IDX_TOT_LITERS_5 = IDX_UNIT_5,
} ab_ll_byte_index_t;

typedef enum
{
	LL_IDX_TOT_LITERS_6 = IDX_VOLUME_0,
	LL_IDX_TOT_LITERS_7 = IDX_VOLUME_1,
	LL_IDX_TOT_LITERS_8 = IDX_VOLUME_2,
	LL_IDX_TOT_LITERS_9 = IDX_VOLUME_3,
	LL_IDX_TOT_LITERS_10 = IDX_VOLUME_4,
	LL_IDX_TOT_LITERS_11 = IDX_VOLUME_5,
	LL_IDX_TOT_LITERS_12 = IDX_VOLUME_6,
} bb_ll_byte_index_t;

typedef union
{
	struct
	{
		uint8_t LS : 4; // first 4 bits
		uint8_t MS : 4; // last 4 bits
	};
	uint8_t u8int;
} tu_byte_t;

typedef struct
{
	uint8_t id;
	tu_byte_t ab[DATA_SET_SIZE];
	tu_byte_t bb[DATA_SET_SIZE];
} capture_data_t;

static volatile QueueHandle_t capture_queue = NULL;
static QueueHandle_t *ptr_send_que = NULL;

// display tap 1
static volatile bool start_display_1 = false;
static volatile uint8_t bits1_display_1 = 0, bits2_display_1 = 0;
static capture_data_t capture_display_1 = {.id = TX_ID_DIS1_DATA};

// display tap 2
static volatile bool start_display_2 = false;
static volatile uint8_t bits1_display_2 = 0, bits2_display_2 = 0;
static capture_data_t capture_display_2 = {.id = TX_ID_DIS2_DATA};

static const char *TAG = "hong6";

static void pin_interrupt_enable_dis_1();
static void pin_interrupt_enable_dis_2();
static void pin_interrupt_disable_dis_1();
static void pin_interrupt_disable_dis_2();

static void IRAM_ATTR bit_read_dis_1(void *arg)
{
	bits1_display_1 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_1) | (uint8_t)(bits1_display_1 << 1));
	bits2_display_1 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_1) | (uint8_t)(bits2_display_1 << 1));
}

static void IRAM_ATTR bits_read_end_dis_1(void *arg)
{
	const uint8_t index1 = (uint8_t)((bits1_display_1 & 0xF));
	const uint8_t index2 = (uint8_t)((bits2_display_1 & 0xF));

	switch (index1)
	{
	case 0x0:
		start_display_1 = true;
		__attribute__((fallthrough)); // Explicit fallthrough
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
		if (start_display_1)
		{
			capture_display_1.ab[index1].u8int = bits1_display_1;
			capture_display_1.bb[index2].u8int = bits2_display_1;
		}
		break;
	case 0xD:
		if (start_display_1)
		{
			capture_display_1.ab[index1].u8int = bits1_display_1;
			capture_display_1.bb[index2].u8int = bits2_display_1;	
			pin_interrupt_disable_dis_1();
			xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
		}
		break;

	default:
		break;
	}
	// clear bit memory
	bits1_display_1 = 0;
	bits2_display_1 = 0;
}

static void IRAM_ATTR bit_read_dis_2(void *arg) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
	bits1_display_2 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_2) | (uint8_t)(bits1_display_2 << 1));
	bits2_display_2 = (uint8_t)((uint8_t)(c_pin_sdata2_read_dis_2) | (uint8_t)(bits2_display_2 << 1));
}

static void IRAM_ATTR bits_read_end_dis_2(void *arg) // ICACHE_RAM_ATTR
{
	const uint8_t index1 = (uint8_t)((bits1_display_2 & 0xF));
	const uint8_t index2 = (uint8_t)((bits2_display_2 & 0xF));

	switch (index1)
	{
	case 0x0:
		start_display_2 = true;
		__attribute__((fallthrough)); // Explicit fallthrough
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
		if (start_display_2)
		{
			capture_display_2.ab[index1].u8int = bits1_display_2;
			capture_display_2.bb[index2].u8int = bits2_display_2;
		}		
		break;
	case 0xD:
		if (start_display_2)
		{
			capture_display_2.ab[index1].u8int = bits1_display_2;
			capture_display_2.bb[index2].u8int = bits2_display_2;
			pin_interrupt_disable_dis_2();
			xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
		}
	default:
		break;
	}
	// clear bit memory
	bits1_display_2 = 0;
	bits2_display_2 = 0;
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
static void get_display_data(display_data_t *dd, tu_byte_t *ab, tu_byte_t *bb)
{
	display_data_t data = {};

	data.flags.bits.select_ll = (bool)(ab[LL_IDX_LL_1].MS == DIS_CHARACTOR_L && ab[LL_IDX_LL_2].MS == DIS_CHARACTOR_L);
	data.flags.bits.select_l = (bool)(ab[IDX_SELECT_L].MS == DIS_CHARACTOR_L);
	data.flags.bits.select_p = (bool)(bb[IDX_SELECT_P].MS == DIS_CHARACTOR_P);

	data.flags.bits.start_stop = (bool)(bb[IDX_START_STOP].MS == DIS_CHARACTOR_1);

	for (int i = 0; i < DATA_SET_SIZE; i++)
	{
		// check for byte index in ab[] bytes
		switch (i)
		{
		case IDX_UNIT_0:
		case IDX_UNIT_1:
		case IDX_UNIT_2:
		case IDX_UNIT_3:
		case IDX_UNIT_4:
		case IDX_UNIT_5:
			if (ab[i].LS != i)
			{
				data.error.bits.unitprice = true;
				data.error.bits.index = true;
			}
			break;
		case IDX_TOTAL_0:
		case IDX_TOTAL_1:
		case IDX_TOTAL_2:
		case IDX_TOTAL_3:
		case IDX_TOTAL_4:
		case IDX_TOTAL_5:
		case IDX_TOTAL_6:
		case IDX_TOTAL_7:
			if (ab[i].LS != i)
			{
				data.error.bits.totprice = true;
				data.error.bits.index = true;
			}
			break;

		default:
			break;
		}

		// check for byte index in bb[] bytes
		switch (i)
		{
		case IDX_VOLUME_0:
		case IDX_VOLUME_1:
		case IDX_VOLUME_2:
		case IDX_VOLUME_3:
		case IDX_VOLUME_4:
		case IDX_VOLUME_5:
		case IDX_VOLUME_6:
			if(bb[i].LS != i)
			{
				data.error.bits.volume = true;
				data.error.bits.index = true;
			}
			break;

		default:
			break;
		}

		if (ab[i].MS > 9)
			ab[i].MS = 0;
		if (bb[i].MS > 9)
			bb[i].MS = 0;
	}

	/*if Total Liter option is selected, calculate it*/
	if (data.flags.bits.select_ll)
	{
		data.total_liters = (uint64_t)(bb[LL_IDX_TOT_LITERS_12].MS) * 1000000000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_11].MS) * 100000000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_10].MS) * 10000000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_9].MS)  * 1000000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_8].MS)  * 100000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_7].MS)  * 10000000 +
							(uint64_t)(bb[LL_IDX_TOT_LITERS_6].MS)  * 1000000 +
							
							(uint64_t)(ab[LL_IDX_TOT_LITERS_5].MS)  * 100000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_4].MS)  * 10000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_3].MS)  * 1000 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_2].MS)  * 100 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_1].MS)  * 10 +
							(uint64_t)(ab[LL_IDX_TOT_LITERS_0].MS);
	}
	else
	{
		data.unit_price = (uint32_t)(ab[IDX_UNIT_5].MS) * 100000 +
						  (uint32_t)(ab[IDX_UNIT_4].MS) * 10000 +
						  (uint32_t)(ab[IDX_UNIT_3].MS) * 1000 +
						  (uint32_t)(ab[IDX_UNIT_2].MS) * 100 +
						  (uint32_t)(ab[IDX_UNIT_1].MS) * 10 +
						  (uint32_t)(ab[IDX_UNIT_0].MS);

		data.total_price = (uint32_t)(ab[IDX_TOTAL_7].MS) * 10000000 +
						   (uint32_t)(ab[IDX_TOTAL_6].MS) * 1000000 +
						   (uint32_t)(ab[IDX_TOTAL_5].MS) * 100000 +
						   (uint32_t)(ab[IDX_TOTAL_4].MS) * 10000 +
						   (uint32_t)(ab[IDX_TOTAL_3].MS) * 1000 +
						   (uint32_t)(ab[IDX_TOTAL_2].MS) * 100 +
						   (uint32_t)(ab[IDX_TOTAL_1].MS) * 10 +
						   (uint32_t)(ab[IDX_TOTAL_0].MS);

		data.volume_l = (uint32_t)(bb[IDX_VOLUME_6].MS) * 1000000 +
						(uint32_t)(bb[IDX_VOLUME_5].MS) * 100000 +
						(uint32_t)(bb[IDX_VOLUME_4].MS) * 10000 +
						(uint32_t)(bb[IDX_VOLUME_3].MS) * 1000 +
						(uint32_t)(bb[IDX_VOLUME_2].MS) * 100 +
						(uint32_t)(bb[IDX_VOLUME_1].MS) * 10 +
						(uint32_t)(bb[IDX_VOLUME_0].MS);

		// decimal point of total price and unit price is equal, so consider volume decimal points only
		uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
		gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
		if (gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR)) // if total price should match with unit_price*valume. Check for the tolerance
		{
			data.error.bits.price_gap = true;
		}
	}

	memcpy((void *)dd, (void *)&data, sizeof(display_data_t));
}

static bool memcmp_dataset(capture_data_t *data1, capture_data_t *data2)
{
    for (size_t i = 0; i < DATA_SET_SIZE; i++)
    {
        if(((*data1).ab[i].u8int) != ((*data2).ab[i].u8int))
            return true;
		if(i <= IDX_VOLUME_0 && ((*data1).bb[i].u8int) != ((*data2).bb[i].u8int))
			return true;
    }
    return false;
}

static void data_send_task(void *arg)
{
	TickType_t ticks_last[TX_ID_DIS_DATA_SIZE] = {xTaskGetTickCount(), xTaskGetTickCount()};
	capture_data_t capture_now = {};
	capture_data_t capture_dis[TX_ID_DIS_DATA_SIZE] = {};
	data_packet_t display_data = {
		.display = DIS_HONGYANG_8_DIGIT,
		.length = sizeof(display_data_t)};
	while (1)
	{
		if (xQueueReceive(capture_queue, &capture_now, pdMS_TO_TICKS(10)))
		{
			const size_t dis_id = capture_now.id;
			// if received data is different from what we have and it is within time range for send
			const TickType_t ticks_now = xTaskGetTickCount();
			// send data on timeout or mismatch with previous one
			if (ticks_now - ticks_last[dis_id] > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp_dataset(capture_dis + dis_id, &capture_now))
			{
				const capture_data_t now_temp = capture_now;
				ticks_last[dis_id] = ticks_now;
				display_data.pck_id = dis_id;
				get_display_data((display_data_t *)(display_data.ab_data), capture_now.ab, capture_now.bb);
				if ((((display_data_t *)(display_data.ab_data))->error.u8int & settings.error_mask.u8int) == 0) // if retain errors are none zero, do not send
				{
					memcpy(capture_dis[dis_id].ab, now_temp.ab, sizeof(now_temp.ab));	 // copy correct packet for next comparison
					memcpy(capture_dis[dis_id].bb, now_temp.bb, sizeof(now_temp.bb));	 // copy correct packet for next comparison
					xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10)); // if queue is full, wait 10ms until it gets clear
				}
				// const display_data_t *dis = (display_data_t *)(display_data.ab_data);
				// LOG_PRINT("dis=%d\tunit=%ld\ttotal=%ld\tvolume=%ld\terr=%d\r\n\r\n", capture_now.id, dis->unit_price, dis->total_price, dis->volume_l, dis->error.u8int);
			}
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
		    get_display_data((display_data_t *)(display_data.ab_data), temp.ab, temp.bb);
		    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
		}
		if ((xTaskGetTickCount() - ticks_last[TX_ID_DIS2_DATA]) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
		{
			capture_data_t temp = capture_dis[TX_ID_DIS2_DATA];
		    ticks_last[TX_ID_DIS2_DATA] = xTaskGetTickCount();
		    display_data.pck_id = TX_ID_DIS2_DATA;
		    get_display_data((display_data_t *)(display_data.ab_data), temp.ab, temp.bb);
		    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
		}
	}
	vTaskDelete(NULL);
}

static void pin_interrupt_enable_dis_1()
{
	start_display_1 = false;
	memset(capture_display_1.ab, 0xFF, sizeof(capture_display_1.ab));
	memset(capture_display_1.bb, 0xFF, sizeof(capture_display_1.ab));
	gpio_set_intr_type(D1_SCLK, GPIO_INTR_POSEDGE);
	gpio_set_intr_type(D1_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_enable_dis_2()
{
	start_display_2 = false;
	memset(capture_display_2.ab, 0xFF, sizeof(capture_display_1.ab));
	memset(capture_display_2.bb, 0xFF, sizeof(capture_display_1.bb));
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

esp_err_t display_hongyang_8_digit_init(QueueHandle_t *send_que)
{
    esp_err_t ret = ESP_OK;
	gpio_config_t io_conf;

	// set display enable pin output and turn off. set chip select signals out
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = BIT64(DIS_ENB) | BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// Set two clock positive edge intterupt pin inputs
	io_conf.intr_type = GPIO_INTR_POSEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT64(D1_SCLK) | BIT64(D1_RCLK) | BIT64(D2_SCLK) | BIT64(D2_RCLK);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// Set DATA1 and DATA2 bits reading inputs, set chip select signals inputs
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT64(D1_SDATA1) | BIT64(D1_SDATA2) | BIT64(D2_SDATA1) | BIT64(D2_SDATA2);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // Set set chip select signals inputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_IN_CS) | BIT64(D2_IN_CS);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// install gpio isr service
	ESP_ERROR_GOTO(ret, end, gpio_install_isr_service(0));
	// hook isr handler for specific gpio pin
	ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_SCLK, bit_read_dis_1, NULL));
	ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, bits_read_end_dis_1, NULL));
	ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_SCLK, bit_read_dis_2, NULL));
	ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, bits_read_end_dis_2, NULL));

	capture_queue = xQueueCreate(20, sizeof(capture_data_t));
	if (capture_queue == NULL)
	{
		ret = ESP_FAIL;
		goto end;
	}

	ptr_send_que = send_que;

	bits1_display_1 = 0;
	bits2_display_1 = 0;

	bits1_display_2 = 0;
	bits2_display_2 = 0;

	if (xTaskCreate(data_send_task, "data_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
	{
		ret = ESP_FAIL;
		goto end;
	}
	gpio_set_level(DIS_ENB, true);
	ESP_LOGI(TAG, "Staring Hongyang 8 display\r\n");
end:
	return ret;
}
