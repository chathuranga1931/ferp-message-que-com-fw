#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sys_types.h"
#include "settings.h"
#include "board_io.h"
#include "device.h"

// #define LOG_OUT

#define PRICE_TO_DIS_COUNTS(x) (x * 100)
#define VOLUME_COUNTS 1000ULL           

#define TIMER_TOUT_CAPTURE_US ((60+30)*1000) // 1.5 packet gap
#define TIMER_TOUT_US (6 * 1000)
#define DATA_RX_MAX 1024
#define DATA_SET_SIZE 19
#define PACKET_MAX_SIZE 200

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

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
    IDX_SELECT_P = IDX_SELECT_L,
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
} tu_byte_t;

typedef union __attribute__((packed))
{
	tu_byte_t tu8int[2];
	uint16_t u16int;
} tu_word_t;

static QueueHandle_t *ptr_send_que = NULL;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[DATA_RX_MAX] = {};
static tu_word_t dis1_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis1_timer = NULL, dis1_timer_cs = NULL;
static volatile bool dis1_cs_level, dis1_cs_tout;
static volatile uint32_t dis1_pulse;

WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[DATA_RX_MAX] = {};
static tu_word_t dis2_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis2_timer = NULL, dis2_timer_cs = NULL;
static volatile bool dis2_cs_level, dis2_cs_tout;
static volatile uint32_t dis2_pulse;

static const uint8_t index_map[][2] =
{
    { 0x45, 0xFF},
    { 0x35, 0xFF},
    { 0x25, 0xFF},
    { 0x15, 0xFF},
    { 0x05, 0xFF},
    { 0xB5, 0xFF},
    { 0xA5, 0xFF},
    { 0x95, 0xFF},
    { 0x85, 0xFF},
    { 0x75, 0xFF},
    { 0x65, 0xFF},
    { 0x55, 0xFF},
    { 0xF2, 0xFF},
    { 0xF1, 0xFF},
    { 0xF0, 0xFF},
    { 0xF3, 0xFF},
    { 0xE5, 0xFF},
    { 0xD5, 0xFF},
    { 0xC5, 0xFF}
};

static const char *TAG = "cens7cs";

static void task_spi_data(void *arg);
static void timer_rclk_tout_dis1(void *arg);
static void gpio_rclk_done_dis1(void *arg);
static void timer_rclk_tout_dis2(void *arg);
static void gpio_rclk_done_dis2(void *arg);
static uint8_t get_display_data(display_data_t *dd, tu_word_t *ab);
static int extract_12bit_words(const uint8_t *in_data, size_t num_words, tu_word_t *out_words);

IRAM_ATTR static void timer_cs_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    dis1_cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    dis1_cs_level = true;
}
IRAM_ATTR static void gpio_rclk_done_dis1(void *arg)
{
    dis1_pulse++;
    if (esp_timer_is_active(dis1_timer))
    {
        esp_timer_restart(dis1_timer, TIMER_TOUT_US);
    }
    else
    {
        esp_timer_start_once(dis1_timer, TIMER_TOUT_US);
    } 
}

IRAM_ATTR static void timer_cs_tout_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
    dis2_cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
    dis2_cs_level = true;
}
IRAM_ATTR static void gpio_rclk_done_dis2(void *arg)
{
    dis2_pulse++;
    if (esp_timer_is_active(dis2_timer))
    {
        esp_timer_restart(dis2_timer, TIMER_TOUT_US);
    }
    else
    {
        esp_timer_start_once(dis2_timer, TIMER_TOUT_US);
    } 
}

static uint8_t get_display_data(display_data_t *dd, tu_word_t *ab)
{
    // data_error_t error = {};
    display_data_t data = {};

    /*Check user enters Total Liter cosumption display*/
	data.flags.bits.select_ll = (bool)(ab[LL_IDX_LL_1].tu8int[1].LS == DIS_CHARACTOR_L && ab[LL_IDX_LL_2].tu8int[1].LS == DIS_CHARACTOR_L);
	data.flags.bits.select_l = (bool)(ab[IDX_SELECT_L].tu8int[1].LS == DIS_CHARACTOR_L);
	data.flags.bits.select_p = (bool)(ab[IDX_SELECT_P].tu8int[1].LS == DIS_CHARACTOR_P);

	for (size_t i = 0; i < DATA_SET_SIZE; i++)
	{
		switch (i)
		{
		case IDX_UNIT_0:
		case IDX_UNIT_1:
		case IDX_UNIT_2:
		case IDX_UNIT_3:
		case IDX_UNIT_4:
        {
            const uint8_t mask = index_map[i][1];
			if((ab[i].tu8int[0].u8int & mask) != (index_map[i][0] & mask))
			{
				data.error.bits.unitprice = true;
                data.error.bits.index = true;
			}
        }
		break;
		case IDX_TOTAL_0:
		case IDX_TOTAL_1:
		case IDX_TOTAL_2:
		case IDX_TOTAL_3:
		case IDX_TOTAL_4:
		case IDX_TOTAL_5:
		case IDX_TOTAL_6:
        {
            const uint8_t mask = index_map[i][1];
			if((ab[i].tu8int[0].u8int & mask) != (index_map[i][0] & mask))
			{
				data.error.bits.totprice = true;
				data.error.bits.index = true;
			}
        }
		break;
		case IDX_VOLUME_0:
		case IDX_VOLUME_1:
		case IDX_VOLUME_2:
		case IDX_VOLUME_3:
		case IDX_VOLUME_4:
		case IDX_VOLUME_5:
		case IDX_VOLUME_6:
        {
            const uint8_t mask = index_map[i][1];
			if((ab[i].tu8int[0].u8int & mask) != (index_map[i][0] & mask))
			{
				data.error.bits.volume = true;
				data.error.bits.index = true;
			}
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
	if (data.flags.bits.select_ll)
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
			data.error.bits.price_gap = true;
		}
	}


    // copy packet
    memcpy((void *)dd, (void *)&data, sizeof(display_data_t));

    return data.error.u8int;
}

static uint64_t extract_bits(const uint8_t *bytes, int bit_pos, int num_bits) 
{
    // sanatise
    if(bit_pos < 0 || num_bits < 0)
        return 0;

    // Index of the first byte and offset of first bit within that byte
    int byte_idx = bit_pos / 8;
    int bit_offset = bit_pos % 8;

    // Calculate how many bytes we need to read (max 5 for 32 bits)
    int total_bits = bit_offset + num_bits;
    int bytes_needed = (total_bits + 7) / 8;

    // Read enough bytes into a buffer
    uint64_t buffer = 0;
    for (int i = 0; i < bytes_needed; i++) {
        buffer = (buffer << 8) | bytes[byte_idx + i];
    }

    // Shift buffer to align the desired bits at the rightmost position
    int shift = (bytes_needed * 8) - bit_offset - num_bits;
    uint64_t result = (buffer >> shift) & ((1UL << num_bits) - 1);

    return result;
}

// uint16_t extract_12bit(const uint8_t *bytes, int bit_pos)
// {
//     // Find the byte index and the bit offset within that byte
//     int byte_idx = bit_pos / 8;
//     int bit_offset = bit_pos % 8;

//     // Read 3 bytes to cover the case where bits span across bytes
//     uint32_t buffer = (bytes[byte_idx] << 16) | (bytes[byte_idx + 1] << 8) | (bytes[byte_idx + 2]);

//     // Shift left to remove leading bits, then right-align to get 12 bits
//     uint16_t value = (buffer >> (12 - bit_offset)) & 0xFFF;

//     return value;
// }

static int extract_12bit_words(const uint8_t *in_data, size_t num_words, tu_word_t *out_words)
{
    int err_word = -1;
    for (size_t i = 0, idx = 0; i < num_words; ++i)
    {
        tu_word_t temp;
        size_t bit_pos = idx;
        size_t num_bits = 12;
        // looking for forward index match
        for (size_t j = 0; j < 4; j++,bit_pos++)
        {
            temp.u16int = extract_bits(in_data, bit_pos, num_bits);
            if(temp.tu8int[0].u8int == index_map[i][0])
                goto set;
            // printf("try+ %d,%d\t0x%.4X\r\n", j+1, bit_pos, temp.u16int);
        }
        // looking backward index match
        bit_pos = idx;
        for (size_t j = 0; j < 4; j++,bit_pos--,num_bits--)
        {
            temp.u16int = extract_bits(in_data, bit_pos, num_bits);
            if(temp.tu8int[0].u8int == index_map[i][0])
                goto set;
            // printf("try- %d,%d,%d\t0x%.4X\r\n", j+1, bit_pos, num_bits, temp.u16int);
        }

        // printf("got invalid %d byte:0x%.4X\r\n", i);
        bit_pos = idx;
        num_bits = 12;
        temp.u16int = 0;
        err_word = i;
    set:
        idx = bit_pos + num_bits;
        out_words[i].u16int = temp.u16int;
    }
    return err_word;
}

// char send_str[100];

static void task_spi_data(void *arg)
{
    esp_err_t ret = ESP_OK;
    int err_word;
    TickType_t ticks_now;
    display_data_t capture_now = {};
	data_packet_t display_data = {
		.display = DIS_CENSTAR_7CS_DIGIT,
		.length = sizeof(display_data_t)};

    spi_slave_transaction_t spi_data_dis1 = {
        .length = sizeof(dis1_recvbuf) * 8 * sizeof(*dis1_recvbuf),
        .rx_buffer = dis1_recvbuf,
    };
    display_data_t cap_data_dis1 = {};
    TickType_t ticks_last_dis1 = xTaskGetTickCount();
    memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
    gpio_set_level(D1_OUT_CS, false); // start capturing
    dis1_cs_level = false;
    dis1_pulse = 0;
    
    spi_slave_transaction_t spi_data_dis2 = {
        .length = sizeof(dis2_recvbuf) * 8 * sizeof(*dis2_recvbuf),
        .rx_buffer = dis2_recvbuf
    };
    display_data_t cap_data_dis2 = {};
    TickType_t ticks_last_dis2 = xTaskGetTickCount();
    memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
    gpio_set_level(D2_OUT_CS, false); // start capturing
    dis2_cs_level = false;
    dis2_pulse = 0;

    while (1)
    {
        /* Capture data from display 1 */
        if(dis1_cs_level)
        {
            dis1_cs_tout = false;            
            dis1_pulse = 0;
            memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
            spi_data_dis1.trans_len = 0;
            esp_timer_stop(dis1_timer);
            gpio_set_level(D1_OUT_CS, false);
            esp_timer_start_once(dis1_timer_cs, TIMER_TOUT_CAPTURE_US);
            //reading loaded data to spi
            ret = spi_slave_transmit(D1_SPI_HOST, &spi_data_dis1, pdMS_TO_TICKS(2 * 1000));
            // spi_slave_transaction_t *trans_desc = NULL;
            // ret = spi_slave_get_trans_result(D1_SPI_HOST, &trans_desc, pdMS_TO_TICKS(10));
            esp_timer_stop(dis1_timer_cs);
            esp_timer_stop(dis1_timer);
            if (dis1_cs_tout || ret != ESP_OK || ((sizeof(dis1_capture) * 8) > spi_data_dis1.trans_len) || ((PACKET_MAX_SIZE*8) < spi_data_dis1.trans_len))
            {
                ESP_LOGI(TAG, "dis1 cstout:%d, fail:0x%.2x len:%d,%d pulses:%ld", dis1_cs_tout, ret, spi_data_dis1.trans_len / 8, spi_data_dis1.trans_len, dis1_pulse);
                goto end_dis1;
            }
            // ESP_LOGI(TAG, "%d,%ld", spi_data_dis1.trans_len, dis1_pulse);
            // for (size_t i = 0; i < 33; i++)
            // {
            //     printf("0x%.2X, ", dis1_recvbuf[i]);
            // }
            // printf("\r\n");
            
            // int str_len = 0;
            // send_str[0] = '\0';
            // for (size_t i = 0; i < 33; i++)
            // {
            //     str_len += sprintf(send_str + str_len, "0x%.2x, ", dis1_recvbuf[i]);
            // }
            
            // LOG_PRINT("len:%d i:%ld - %s\r\n", spi_data_dis1.trans_len / 8, dis1_pulse, send_str);

            // clear buffer
            memset(dis1_capture, 0, sizeof(dis1_capture));
            // decode packet
            err_word = extract_12bit_words(dis1_recvbuf, sizeof(dis1_capture)/sizeof(*dis1_capture), dis1_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis1 errror:%d", err_word); 
                goto end_dis1;
            }
            //decode packet
            get_display_data(&capture_now, dis1_capture);
            // print logs
    #ifdef LOG_OUT
            if(capture_now.flags.bits.select_ll)
                ESP_LOGI(TAG, "dis1\tunit=%.2f\ttotalizer=%lld", capture_now.unit_price / 100.0, capture_now.total_liters);
            else
                ESP_LOGI(TAG, "dis1\tunit=%.2f\ttotal=%.3f\tvolume=%.3f", capture_now.unit_price / 100.0, capture_now.total_price / 100.0, capture_now.volume_l / 1000.0);
    #endif
            // compare difference timeout and send
            // if received data is different from what we have and it is within time range for send
            ticks_now = xTaskGetTickCount();
            // send data on timeout or mismatch with previous one
            if (ticks_now - ticks_last_dis1 > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(&cap_data_dis1, &capture_now, sizeof(display_data_t)))
            {
                ticks_last_dis1 = ticks_now;
                display_data.pck_id = TX_ID_DIS1_DATA;
                memcpy(display_data.ab_data, &capture_now, sizeof(display_data_t));
                if((capture_now.error.u8int & settings.error_mask.u8int) == 0) //if retain errors are none zero, do not send
                {
                    memcpy(&cap_data_dis1, &capture_now, sizeof(display_data_t)); //copy correct packet for next comparison
                    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10)); // if queue is full, wait 10ms until it gets clear
                }
            }
        end_dis1:
            dis1_cs_level = false;
            // dis1_pulse = 0;
            // memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
            // spi_data_dis1.trans_len = 0;
            // gpio_set_level(D1_OUT_CS, false); // enable back SPI
        }

        /* Capture data from display 2 */
        if(dis2_cs_level)
        {
            dis2_cs_tout = false;
            dis2_pulse = 0;
            spi_data_dis2.trans_len = 0;
            memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
            esp_timer_stop(dis2_timer);
            gpio_set_level(D2_OUT_CS, false); // enable back SPI
            esp_timer_start_once(dis2_timer_cs, TIMER_TOUT_CAPTURE_US);
            ret = spi_slave_transmit(D2_SPI_HOST, &spi_data_dis2, pdMS_TO_TICKS(2 * 1000));
            esp_timer_stop(dis2_timer_cs);
            esp_timer_stop(dis2_timer);
            if (dis2_cs_tout || ret != ESP_OK || ((sizeof(dis2_capture) * 8) > spi_data_dis2.trans_len) || (((PACKET_MAX_SIZE*8) < spi_data_dis2.trans_len)))
            {
                ESP_LOGI(TAG, "dis2 cstout:%d, fail:0x%.2x len:%d,%d pulses:%ld", dis2_cs_tout, ret, spi_data_dis2.trans_len / 8, spi_data_dis2.trans_len, dis2_pulse);
                goto end_dis2;
            }
            // clear buffer
            memset(dis2_capture, 0, sizeof(dis2_capture));
            // decode packet;
            err_word = extract_12bit_words(dis2_recvbuf, sizeof(dis2_capture)/sizeof(*dis2_capture), dis2_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis2 errror:%d", err_word); 
                goto end_dis2;
            }
            //decode packet
            get_display_data(&capture_now, dis2_capture);
            // print logs
    #ifdef LOG_OUT
            if(capture_now.flags.bits.select_ll)
                ESP_LOGI(TAG, "dis2\tunit=%.2f\ttotalizer=%lld", capture_now.unit_price / 100.0, capture_now.total_liters);
            else
                ESP_LOGI(TAG, "dis2\tunit=%.2f\ttotal=%.3f\tvolume=%.3f", capture_now.unit_price / 100.0, capture_now.total_price / 100.0, capture_now.volume_l / 1000.0);
    #endif
            // compare difference timeout and send
            // if received data is different from what we have and it is within time range for send
            ticks_now = xTaskGetTickCount();
            // send data on timeout or mismatch with previous one
            if (ticks_now - ticks_last_dis2 > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(&cap_data_dis2, &capture_now, sizeof(display_data_t)))
            {
                ticks_last_dis2 = ticks_now;
                display_data.pck_id = TX_ID_DIS2_DATA;
                memcpy(display_data.ab_data, &capture_now, sizeof(display_data_t));
                if((capture_now.error.u8int & settings.error_mask.u8int) == 0) //if retain errors are none zero, do not send
                {
                    memcpy(&cap_data_dis2, &capture_now, sizeof(display_data_t)); //copy correct packet for next comparison
                    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10)); // if queue is full, wait 10ms until it gets clear
                }
            }
        end_dis2:
            dis2_cs_level = false;
        }

		/* Timeout handling for Display 1 and Display 2 */
        ticks_now = xTaskGetTickCount();
        if ((ticks_now - ticks_last_dis1) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
            ticks_last_dis1 = ticks_now;
            display_data.pck_id = TX_ID_DIS1_DATA;
            memcpy(display_data.ab_data, &cap_data_dis1, sizeof(display_data_t));
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
		if ((ticks_now - ticks_last_dis2) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
            ticks_last_dis2 = ticks_now;
            display_data.pck_id = TX_ID_DIS2_DATA;
            memcpy(display_data.ab_data, &cap_data_dis2, sizeof(display_data_t));
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
}

esp_err_t display_censtar_7cs_digit_init(QueueHandle_t *send_queue)
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

	// // set chip select signals out
	// io_conf.intr_type = GPIO_INTR_DISABLE;
	// io_conf.mode = GPIO_MODE_OUTPUT;
	// io_conf.pin_bit_mask = BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS);
	// io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	// io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	// ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// Set two clock positive edge intterupt pin inputs
	io_conf.intr_type = GPIO_INTR_POSEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT64(D1_RCLK) | BIT64(D2_RCLK);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// Set DATA2 pins as pull ups because no use
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT64(D1_SDATA2) | BIT64(D2_SDATA2);
	io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

	// install gpio isr service
	ESP_ERROR_GOTO(ret, end, gpio_install_isr_service(0));

    //Init SPI for Display 1
    ESP_ERROR_GOTO(ret, end, spi_slave_initialize(D1_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = D1_SCLK, // Clock
        .mosi_io_num = D1_SDATA1,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
    }, &(const spi_slave_interface_config_t){
        .mode = 3, // 0 for simulator, 3 for real device
        .spics_io_num = D1_IN_CS,
        .queue_size = 4,
        .flags = 0,
    }, SPI_DMA_CH1));

    //Init SPI for Display 2
    ESP_ERROR_GOTO(ret, end, spi_slave_initialize(D2_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = D2_SCLK, // Clock
        .mosi_io_num = D2_SDATA1,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
    }, &(const spi_slave_interface_config_t){
        .mode = 3, // 0 for simulator, 3 for real device
        .spics_io_num = D2_IN_CS,
        .queue_size = 4,
        .flags = 0,
    }, SPI_DMA_CH2));

    ESP_ERROR_GOTO(ret, end, esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_dis_1",
        .callback = timer_rclk_tout_dis1,
    }, &dis1_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_cs_dis_1",
        .callback = timer_cs_tout_dis1,
    }, &dis1_timer_cs));
    // attach interrupt for RCLK signal
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, gpio_rclk_done_dis1, NULL));

    ESP_ERROR_GOTO(ret, end, esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_dis_2",
        .callback = timer_rclk_tout_dis2,
    }, &dis2_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_cs_dis_2",
        .callback = timer_cs_tout_dis2,
    }, &dis2_timer_cs));
    // attach interrupt for RCLK signal
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, gpio_rclk_done_dis2, NULL));

	ptr_send_que = send_queue;

    if(xTaskCreate(task_spi_data, "task_spi_data", 8 * 1024, NULL, 8, NULL) == pdFALSE)
    {
		ret = ESP_FAIL;
		goto end;
    }    

	gpio_set_level(DIS_ENB, true);
	ESP_LOGI(TAG, "Staring Censtar 7 CS display\r\n");
end:
	return ret;
}
