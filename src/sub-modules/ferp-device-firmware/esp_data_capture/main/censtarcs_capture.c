#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_err.h"
#include "hongyang_8_digit.h"
#include "esp_timer.h"

#if CONFIG_LOG_MAXIMUM_LEVEL > 2
    #define LOG_OUT
#endif

#define TIMER_TOUT_US (5 * 1000)

#define PRICE_GAP_LKR 10 // allowable price gap to not to mark any error
#define PRICE_TO_DIS_COUNTS(x) (x * 100) // in Longfeng 8 digit, two decimal point is showing.
#define VOLUME_COUNTS 1000ULL            // in Longfeng 8 digit, have tree decimal digits

#define DATA_RX_MAX 1024
#define DATA_SET_SIZE 19
#define PACKET_MAX_SIZE 200

#define DIS1_SPI_HOST SPI2_HOST
#define DIS2_SPI_HOST SPI3_HOST

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

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[DATA_RX_MAX] = {};
static tu_word_t dis1_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis1_timer = NULL;
static bool dis1_cs_level;
static uint32_t dis1_pulse;

#if DIS2_CAPTURE_ENABLE
WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[DATA_RX_MAX] = {};
static tu_word_t dis2_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis2_timer = NULL;
static bool dis2_cs_level;
static uint32_t dis2_pulse;
#endif


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

static const char *TAG = "censtar";

static void task_spi_data(void *arg);
static void timer_rclk_tout_dis1(void *arg);
static void gpio_rclk_done_dis1(void *arg);
#ifdef DIS2_CAPTURE_ENABLE
static void timer_rclk_tout_dis2(void *arg);
static void gpio_rclk_done_dis2(void *arg);
#endif

static uint8_t get_display_data(dis_capture_t *dd, tu_word_t *ab);
static int extract_12bit_words(const uint8_t *in_data, size_t num_words, tu_word_t *out_words);

void init_censtarcs_capture()
{
    // config RCLK input signal
    ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BIT64(DIS1_IN_RCLK)
#ifdef DIS2_CAPTURE_ENABLE
                        | BIT64(DIS2_IN_RCLK)
#endif
        ,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE}));

    // configure chip select output signal
    ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(DIS1_OUT_CS)
#ifdef DIS2_CAPTURE_ENABLE
                        | BIT64(DIS2_OUT_CS)
#endif
        ,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE}));
    
    //Init SPI for Display 1
    ESP_ERROR_CHECK(spi_slave_initialize(DIS1_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = DIS1_IN_SCLK, // Clock
        .mosi_io_num = DIS1_IN_DATA1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    }, &(const spi_slave_interface_config_t){
        .mode = 3,
        .spics_io_num = DIS1_IN_CS,
        .queue_size = 4,
        .flags = 0, // SPI_SLAVE_RXBIT_LSBFIRST,
    }, SPI_DMA_CH1));

#ifdef DIS2_CAPTURE_ENABLE
    //Init SPI for Display 2
    ESP_ERROR_CHECK(spi_slave_initialize(DIS2_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = DIS2_IN_SCLK, // Clock
        .mosi_io_num = DIS2_IN_DATA1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    }, &(const spi_slave_interface_config_t){
        .mode = 3,
        .spics_io_num = DIS2_IN_CS,
        .queue_size = 4,
        .flags = 0, // SPI_SLAVE_RXBIT_LSBFIRST,
    }, SPI_DMA_CH2));
#endif

    esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_dis_1",
        .callback = timer_rclk_tout_dis1,
    }, &dis1_timer);
    // attach interrupt for RCLK signal
    ESP_ERROR_CHECK(gpio_isr_handler_add(DIS1_IN_RCLK, gpio_rclk_done_dis1, NULL));

#ifdef DIS2_CAPTURE_ENABLE
    esp_timer_create( &(const esp_timer_create_args_t){
        .name = "t_dis_2",
        .callback = timer_rclk_tout_dis2,
    }, &dis2_timer);
    // attach interrupt for RCLK signal
    ESP_ERROR_CHECK(gpio_isr_handler_add(DIS2_IN_RCLK, gpio_rclk_done_dis2, NULL));
#endif

    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(task_spi_data, "task_spi_data", 16 * 1024, NULL, 8, NULL, 0) == pdFALSE);
    ESP_LOGI(TAG, "Done");
}

IRAM_ATTR static void timer_rclk_tout_dis1(void *arg)
{
    gpio_set_level(DIS1_OUT_CS, true);
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

#ifdef DIS2_CAPTURE_ENABLE
IRAM_ATTR static void timer_rclk_tout_dis2(void *arg)
{
    gpio_set_level(DIS2_OUT_CS, true);
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
#endif

static void task_spi_data(void *arg)
{
    esp_err_t ret = ESP_OK;

    dis_capture_t cap_data_dis1 = {};
    spi_slave_transaction_t spi_data_dis1 = {
        .length = sizeof(dis1_recvbuf) * 8 * sizeof(*dis1_recvbuf),
        .rx_buffer = dis1_recvbuf
    };

    memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
    gpio_set_level(DIS1_OUT_CS, false); // start capturing
    dis1_pulse = 0;
#ifdef DIS2_CAPTURE_ENABLE
    spi_slave_transaction_t spi_data_dis2 = {
        .length = sizeof(dis2_recvbuf) * 8 * sizeof(*dis2_recvbuf),
        .rx_buffer = dis2_recvbuf
    };
    dis_capture_t cap_data_dis2 = {};
    memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
    gpio_set_level(DIS2_OUT_CS, false); // start capturing
    dis2_pulse = 0;
#endif

    ESP_LOGW(TAG, "Starting spi task");
    while (1)
    {
        if(dis1_cs_level)
        {
            ret = spi_slave_transmit(DIS1_SPI_HOST, &spi_data_dis1, 1);
            if (ret != ESP_OK || ((sizeof(dis1_capture) * 8) > spi_data_dis1.trans_len) || ((PACKET_MAX_SIZE*8) < spi_data_dis1.trans_len))
            {
                ESP_LOGI(TAG, "dis1 fail:0x%.2x len:%d,%d pulses:%ld", ret, spi_data_dis1.trans_len / 8, spi_data_dis1.trans_len, dis1_pulse);
                goto end_dis1;
            }
            // ESP_LOGI(TAG, "%d,%ld", spi_data_dis1.trans_len, dis1_pulse);
            // for (size_t i = 0; i < 33; i++)
            // {
            //     printf("0x%.2X, ", dis1_recvbuf[i]);
            // }
            // printf("\r\n");

            // clear buffer
            memset(dis1_capture, 0, sizeof(dis1_capture));
            // decode packet
            const int err_word = extract_12bit_words(dis1_recvbuf, sizeof(dis1_capture)/sizeof(*dis1_capture), dis1_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis1 errror:%d", err_word); 
                goto end_dis1;
            }
            //decode packet
            get_display_data(&cap_data_dis1, dis1_capture);
            // copy data into hngyang buffer
            dis1_copy_data(&cap_data_dis1);
#ifdef LOG_OUT
            if(cap_data_dis1.flags.select_ll)
                ESP_LOGI(TAG, "dis1\tunit=%.2f\ttotalizer=%lld", cap_data_dis1.unit_price / 100.0, cap_data_dis1.total_liters);
            else
                ESP_LOGI(TAG, "dis1\tunit=%.2f\ttotal=%.3f\tvolume=%.3f", cap_data_dis1.unit_price / 100.0, cap_data_dis1.total_price / 100.0, cap_data_dis1.volume_l / 1000.0);
#endif
        end_dis1:
            gpio_set_level(DIS1_OUT_CS, false); // enable back SPI
            dis1_cs_level = false;
            dis1_pulse = 0;
            memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
        }

#ifdef DIS2_CAPTURE_ENABLE
        if(dis2_cs_level)
        {
            ret = spi_slave_transmit(DIS2_SPI_HOST, &spi_data_dis2, 1);
            if (ret != ESP_OK || ((sizeof(dis2_capture) * 8) > spi_data_dis2.trans_len) || (((PACKET_MAX_SIZE*8) < spi_data_dis2.trans_len)))
            {
                ESP_LOGI(TAG, "dis2 fail:0x%.2x len:%d,%d pulses:%ld", ret, spi_data_dis2.trans_len / 8, spi_data_dis2.trans_len, dis2_pulse);
                goto end_dis2;
            }
            // ESP_LOGI(TAG, "%d", spi_data_dis2.trans_len)
            // clear buffer
            memset(dis2_capture, 0, sizeof(dis2_capture));
            // decode packet;
            const int err_word = extract_12bit_words(dis2_recvbuf, sizeof(dis2_capture)/sizeof(*dis2_capture), dis2_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis1 errror:%d", err_word); 
                goto end_dis2;
            }
            //decode packet
            get_display_data(&cap_data_dis2, dis2_capture);
            // // copy data into hngyang buffer
            dis2_copy_data(&cap_data_dis2);
#ifdef LOG_OUT
            if(cap_data_dis2.flags.select_ll)
                ESP_LOGI(TAG, "dis2\tunit=%.2f\ttotalizer=%lld", cap_data_dis2.unit_price / 100.0, cap_data_dis2.total_liters);
            else
                ESP_LOGI(TAG, "dis2\tunit=%.2f\ttotal=%.3f\tvolume=%.3f", cap_data_dis2.unit_price / 100.0, cap_data_dis2.total_price / 100.0, cap_data_dis2.volume_l / 1000.0);
#endif
        end_dis2:
            gpio_set_level(DIS2_OUT_CS, false); // enable back SPI
            dis2_cs_level = false;
            dis2_pulse = 0;
            memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
        }
#endif
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
    
}

static uint8_t get_display_data(dis_capture_t *dd, tu_word_t *ab)
{
    // data_error_t error = {};
    dis_capture_t data = {};

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
        {
            const uint8_t mask = index_map[i][1];
			if((ab[i].tu8int[0].u8int & mask) != (index_map[i][0] & mask))
			{
				data.error.err_bit.unitprice = true;
                data.error.err_bit.index = true;
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
				data.error.err_bit.totprice = true;
				data.error.err_bit.index = true;
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
				data.error.err_bit.volume = true;
				data.error.err_bit.index = true;
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


    // copy packet
    memcpy((void *)dd, (void *)&data, sizeof(dis_capture_t));

    return data.error.u8int;
}

uint64_t extract_bits(const uint8_t *bytes, int bit_pos, int num_bits) 
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

uint16_t extract_12bit(const uint8_t *bytes, int bit_pos)
{
    // Find the byte index and the bit offset within that byte
    int byte_idx = bit_pos / 8;
    int bit_offset = bit_pos % 8;

    // Read 3 bytes to cover the case where bits span across bytes
    uint32_t buffer = (bytes[byte_idx] << 16) | (bytes[byte_idx + 1] << 8) | (bytes[byte_idx + 2]);

    // Shift left to remove leading bits, then right-align to get 12 bits
    uint16_t value = (buffer >> (12 - bit_offset)) & 0xFFF;

    return value;
}

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