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

#define LOG_OUT
#define LOG_COUNT 0

#define PRICE_TO_DIS_COUNTS(x) (x * 100) // in Longfeng 8 digit, two decimal point is showing.
#define VOLUME_COUNTS 1000ULL            // in Longfeng 8 digit, have tree decimal digits

#define TIMER_TOUT_CAPTURE_US ((15+5+3)*1000) // 1.5 packet gap
#define TIMER_TOUT_US (1000)
#define DATA_SET_SIZE 24

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

/*      ----------------
      / \      A       / \
     /   \------------/   \
     |   |            |   |
     | F |            | B |
     \   /------------\   /
      \ /      G       \ /
      / \--------------/ \
     /   \            /   \
     |   |            |   |
     | E |            | C |
     \   /------------\   /     _
      \ /      D       \ /    / H \
       -----------------      \ _ /

    bits -> 7, 6, 5, 4, 3, 2, 1, 0
    seg  -> H, A, B, C, D, E, F, G
*/
#define CODE_DIGIT_MAP(XX) \
    XX(0, 0x3F, 0xBF, 0)   \
    XX(1, 0x06, 0x86, 1)   \
    XX(2, 0x5B, 0xDB, 2)   \
    XX(3, 0x4F, 0xCF, 3)   \
    XX(4, 0x66, 0xE6, 4)   \
    XX(5, 0x6D, 0xED, 5)   \
    XX(6, 0x7D, 0xFD, 6)   \
    XX(7, 0x47, 0xC7, 7)   \
    XX(8, 0x7F, 0xFF, 8)   \
    XX(9, 0x6F, 0xEF, 9)
// XX(L, 0x1C, 0x1D, 0)
// XX(P, 0xCE, 0xCF, 0)
// XX(A, 0xEE, 0xEF, 0)
// XX(n, 0x2A, 0x2B, 0)

typedef enum
{
    IDX_UNIT_0 = 0,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_4,
    IDX_UNIT_5,
    IDX_UNIT_6,
    IDX_UNIT_7,

    IDX_VOLUME_0,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
    IDX_VOLUME_6,
    IDX_VOLUME_7,

    IDX_TOTAL_0,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_6,
    IDX_TOTAL_7,

    IDX_SELECT_L = IDX_TOTAL_7,
    IDX_SELECT_P = IDX_SELECT_L,
} byte_index_t;

static QueueHandle_t *ptr_send_que = NULL;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[256] = {};
static uint8_t dis1_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis1_timer = NULL, dis1_timer_cs = NULL;
static volatile bool dis1_cs_level, dis1_cs_tout;
static volatile uint32_t dis1_pulse;

WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[256] = {};
static uint8_t dis2_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis2_timer = NULL, dis2_timer_cs = NULL;
static volatile bool dis2_cs_level, dis2_cs_tout;
static volatile uint32_t dis2_pulse;


static const uint8_t seg7_to_digit[256] = {
    [0x07] = 7,
    [0x87] = 7,
#define XX(NAME, SEG, SEG_DECI, VALUE) [SEG] = VALUE, [SEG_DECI] = VALUE,
    CODE_DIGIT_MAP(XX)
#undef XX
};

static const char *TAG = "longfeng";

static void task_spi_data(void *arg);
static void timer_rclk_tout_dis1(void *arg);
static void gpio_rclk_done_dis1(void *arg);
static void timer_rclk_tout_dis2(void *arg);
static void gpio_rclk_done_dis2(void *arg);

static bool get_display_data(display_data_t *dd, uint8_t *ab);
static int extract_8bit_bytes(const uint8_t *in_data, size_t num_bytes, uint8_t *out_bytes);

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


static bool get_display_data(display_data_t *dd, uint8_t *ab)
{
    bool error = false;
    display_data_t data = {};

    for (int i = 0; i < DATA_SET_SIZE; i++)
    {
        ab[i] = seg7_to_digit[ab[i]];
    }

    const uint32_t unit_price = /*(uint32_t)(ab[IDX_UNIT_7]) * 10000000 +  //last 3 digits comes with error code
                      (uint32_t)(ab[IDX_UNIT_6]) * 1000000 +     //ignoring for dis1_count
                      (uint32_t)(ab[IDX_UNIT_5]) * 100000 +*/
                      (uint32_t)(ab[IDX_UNIT_4]) * 10000 +
                      (uint32_t)(ab[IDX_UNIT_3]) * 1000 +
                      (uint32_t)(ab[IDX_UNIT_2]) * 100 +
                      (uint32_t)(ab[IDX_UNIT_1]) * 10 +
                      (uint32_t)(ab[IDX_UNIT_0]);
    if(unit_price) data.unit_price = unit_price; // copy only none zero.

    data.total_price = (uint32_t)(ab[IDX_TOTAL_7]) * 10000000 +
                       (uint32_t)(ab[IDX_TOTAL_6]) * 1000000 +
                       (uint32_t)(ab[IDX_TOTAL_5]) * 100000 +
                       (uint32_t)(ab[IDX_TOTAL_4]) * 10000 +
                       (uint32_t)(ab[IDX_TOTAL_3]) * 1000 +
                       (uint32_t)(ab[IDX_TOTAL_2]) * 100 +
                       (uint32_t)(ab[IDX_TOTAL_1]) * 10 +
                       (uint32_t)(ab[IDX_TOTAL_0]);

    data.volume_l = (uint32_t)(ab[IDX_VOLUME_7]) * 10000000 +
                    (uint32_t)(ab[IDX_VOLUME_6]) * 1000000 +
                    (uint32_t)(ab[IDX_VOLUME_5]) * 100000 +
                    (uint32_t)(ab[IDX_VOLUME_4]) * 10000 +
                    (uint32_t)(ab[IDX_VOLUME_3]) * 1000 +
                    (uint32_t)(ab[IDX_VOLUME_2]) * 100 +
                    (uint32_t)(ab[IDX_VOLUME_1]) * 10 +
                    (uint32_t)(ab[IDX_VOLUME_0]);
    // //create price gap
    // uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
    // gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
    // // detect price gap error
    // error |= (bool)(gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR));

    // copy packet
    memcpy((void *)dd, (void *)&data, sizeof(display_data_t));

    return !error;
}

static int extract_8bit_bytes(const uint8_t *in_data, size_t num_bytes, uint8_t *out_bytes)
{
    int err_word = -1;

    // copy data directly
    memcpy(out_bytes, in_data, num_bytes);

    // could't find a logic to fix bit issue

    return err_word;
}

static void task_spi_data(void *arg)
{
    esp_err_t ret = ESP_OK;
    int err_word;
    TickType_t ticks_now;
    display_data_t capture_now = {};
	data_packet_t display_data = {
		.display = DIS_LONGFENG_8_DIGIT,
		.length = sizeof(display_data_t)};

    spi_slave_transaction_t spi_data_dis1 = {
        .length = sizeof(dis1_recvbuf) * 8 * sizeof(*dis1_recvbuf),
        .rx_buffer = dis1_recvbuf
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

#ifdef LOG_OUT
    uint32_t dis1_count = 0;
    uint32_t dis2_count = 0;
#endif

    while(1)
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
            esp_timer_stop(dis1_timer_cs);
            esp_timer_stop(dis1_timer);
            if (dis1_cs_tout || ret != ESP_OK || ((DATA_SET_SIZE * 8) != spi_data_dis1.trans_len))
            {
                #ifdef LOG_OUT
                ESP_LOGI(TAG, "dis1 cstout:%d, fail:0x%.2x len:%d,%d"/* pulses:%ld"*/, dis1_cs_tout, ret, spi_data_dis1.trans_len / 8, spi_data_dis1.trans_len/*, dis1_pulse*/);
                #endif
                goto end_dis1;
            }

            // LOG_PRINT("%d\r\n", spi_data_dis1.trans_len);

            // clear buffer
            memset(dis1_capture, 0, sizeof(dis1_capture));

            err_word = extract_8bit_bytes(dis1_recvbuf, sizeof(dis1_capture)/sizeof(*dis1_capture), dis1_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis1 errror:%d", err_word); 
                goto end_dis1;
            }
            
            //decode packet
            get_display_data(&capture_now, dis1_capture);
            // print logs
    #ifdef LOG_OUT
            if(dis1_count > LOG_COUNT)
            {
                // printf("dis1:%d,%ld\t", spi_data_dis1.trans_len, dis1_pulse);
                // for (size_t i = 0; i < sizeof(dis1_capture); i++)
                // {
                //     printf("0x%.2x, ", dis1_recvbuf[i]);
                // }
                // printf("\r\n");
                // const uint32_t total_price = (uint32_t)((((double)cap_data_dis1.unit_price * (double)cap_data_dis1.volume_l)/1000.0)+0.5);
                ESP_LOGI(TAG, "dis1\t%ld\t%ld\t%ld", cap_data_dis1.unit_price, cap_data_dis1.total_price, cap_data_dis1.volume_l);
                dis1_count = 0;
            }
            else
            {
                dis1_count++;
            }
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
            dis1_cs_level = false; // wait for next packet timeout
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
            if (dis2_cs_tout || ret != ESP_OK || ((DATA_SET_SIZE * 8) != spi_data_dis2.trans_len))
            {
                #ifdef LOG_OUT
                ESP_LOGI(TAG, "dis2 cstout:%d"/*, fail:0x%.2x len:%d,%d pulses:%ld"*/, dis2_cs_tout/*, ret, spi_data_dis2.trans_len / 8, spi_data_dis2.trans_len, dis2_pulse*/);
                #endif
                goto end_dis2;
            }
            // clear buffer
            memset(dis2_capture, 0, sizeof(dis2_capture));

            err_word = extract_8bit_bytes(dis2_recvbuf, sizeof(dis2_capture)/sizeof(*dis2_capture), dis2_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis2 errror:%d", err_word); 
                goto end_dis2;
            }
            //decode packet
            get_display_data(&cap_data_dis2, dis2_capture);
            // print logs
    #ifdef LOG_OUT
            if(dis2_count > LOG_COUNT)
            {
                // const uint32_t total_price = (uint32_t)((((double)cap_data_dis2.unit_price * (double)cap_data_dis2.volume_l)/1000.0)+0.5);
                ESP_LOGI(TAG, "dis2\t%ld\t%ld\t%ld", cap_data_dis2.unit_price, cap_data_dis2.total_price, cap_data_dis2.volume_l);
                dis2_count = 0;
            }
            else
            {
                dis2_count++;
            }
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
            dis2_cs_level = false; // wait for next packet timeout
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

esp_err_t display_longfeng_8_digit_init(QueueHandle_t *send_queue)
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

	// Set two clock negative edge intterupt pin inputs
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
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
        .mode = 0, // 0 for simulator, 2 for real device
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
        .mode = 0, // 0 for simulator, 2 for real device
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
    ESP_LOGI(TAG,"Staring Longfeng 8 digit display\r\n");
end:
	return ret;
}
