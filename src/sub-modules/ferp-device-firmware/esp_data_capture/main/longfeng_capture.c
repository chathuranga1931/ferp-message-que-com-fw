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
    // #define LOG_OUT
#endif

#define PRICE_GAP_LKR 10 // allowable price gap to not to mark any error

#define DATA_SET_SIZE 24

#define DIS1_SPI_HOST SPI2_HOST
#define DIS2_SPI_HOST SPI3_HOST
#define TIMER_TOUT_US (1000)

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

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[256] = {};
static uint8_t dis1_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis1_timer = NULL;
static bool dis1_cs_level;
static uint32_t dis1_pulse;

#ifdef DIS2_CAPTURE_ENABLE
WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[256] = {};
static uint8_t dis2_capture[DATA_SET_SIZE];
static esp_timer_handle_t dis2_timer = NULL;
static bool dis2_cs_level;
static uint32_t dis2_pulse;
#endif

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
#ifdef DIS2_CAPTURE_ENABLE
static void timer_rclk_tout_dis2(void *arg);
static void gpio_rclk_done_dis2(void *arg);
#endif

static bool get_display_data(dis_capture_t *dd, uint8_t *ab);
static int extract_8bit_bytes(const uint8_t *in_data, size_t num_bytes, uint8_t *out_bytes);

void init_longfeng_capture()
{
    // config RCLK input signal
    ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
        .intr_type = GPIO_INTR_NEGEDGE,
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

    // Display 1 SPI init
    ESP_ERROR_CHECK(spi_slave_initialize(DIS1_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = DIS1_IN_SCLK, // Clock
        .mosi_io_num = DIS1_IN_DATA1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    }, &(const spi_slave_interface_config_t){
        .mode = 2,
        .spics_io_num = DIS1_IN_CS,
        .queue_size = 4,
        .flags = 0, // SPI_SLAVE_RXBIT_LSBFIRST,
    }, SPI_DMA_CH1));

#ifdef DIS2_CAPTURE_ENABLE
    // Display 2 SPI init
    ESP_ERROR_CHECK(spi_slave_initialize(DIS2_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = DIS2_IN_SCLK, // Clock
        .mosi_io_num = DIS2_IN_DATA1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    }, &(const spi_slave_interface_config_t){
        .mode = 2,
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

    spi_slave_transaction_t spi_data_dis1 = {
        .length = sizeof(dis1_recvbuf) * 8 * sizeof(*dis1_recvbuf),
        .rx_buffer = dis1_recvbuf,
    };
    dis_capture_t cap_data_dis1 = {};
    #ifdef LOG_OUT
    uint32_t dis1_count = 0;
    #endif
    dis1_pulse = 0;

#ifdef DIS2_CAPTURE_ENABLE
    spi_slave_transaction_t spi_data_dis2 = {
        .length = sizeof(dis2_recvbuf) * 8 * sizeof(*dis1_recvbuf),
        .rx_buffer = dis2_recvbuf,
    };
    dis_capture_t cap_data_dis2 = {};
    #ifdef LOG_OUT
    uint32_t dis2_count = 0;
    #endif
    dis2_pulse = 5;
#endif
    ESP_LOGW(TAG, "Starting spi task");
    while (1)
    {
        if (dis1_cs_level)
        {
            ret = spi_slave_transmit(DIS1_SPI_HOST, &spi_data_dis1, 1);
            if (ret != ESP_OK || ((DATA_SET_SIZE * 8) != spi_data_dis1.trans_len))
            {
                #ifdef LOG_OUT
                ESP_LOGI(TAG, "dis1 fail:0x%.2x len:%d,%d pulses:%ld", ret, spi_data_dis1.trans_len / 8, spi_data_dis1.trans_len, dis1_pulse);
                #endif
                goto end_dis1;
            }

            // ESP_LOGI(TAG, "%d", spi_data_dis1.trans_len);

            // clear buffer
            memset(dis1_capture, 0, sizeof(dis1_capture));

            const int err_word = extract_8bit_bytes(dis1_recvbuf, sizeof(dis1_capture)/sizeof(*dis1_capture), dis1_capture);
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
            if(dis1_count > 10)
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
        end_dis1:
            gpio_set_level(DIS1_OUT_CS, false); // enable back SPI
            dis1_cs_level = false;
            dis1_pulse = 0;
            memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
        }
#ifdef DIS2_CAPTURE_ENABLE
        if (dis2_cs_level)
        {
            ret = spi_slave_transmit(DIS2_SPI_HOST, &spi_data_dis2, 1);
            if (ret != ESP_OK || ((DATA_SET_SIZE * 8) != spi_data_dis2.trans_len))
            {
                #ifdef LOG_OUT
                ESP_LOGI(TAG, "dis2 fail:0x%.2x len:%d,%d pulses:%ld", ret, spi_data_dis2.trans_len / 8, spi_data_dis2.trans_len, dis2_pulse);
                #endif
                goto end_dis2;
            }

            // ESP_LOGI(TAG, "%d", spi_data_dis2.trans_len);
            // for (size_t i = 0; i < sizeof(dis2_capture); i++)
            // {
            //     printf("0x%.2x, ", dis2_capture[i]);
            // }
            // printf("\r\n");
            // clear buffer
            memset(dis2_capture, 0, sizeof(dis2_capture));

            const int err_word = extract_8bit_bytes(dis2_recvbuf, sizeof(dis2_capture)/sizeof(*dis2_capture), dis2_capture);
            if(err_word != -1)
            {
                ESP_LOGE(TAG, "dis2 errror:%d", err_word); 
                goto end_dis2;
            }
            //decode packet
            get_display_data(&cap_data_dis2, dis2_capture);

            // copy data into hngyang buffer
            dis2_copy_data(&cap_data_dis2);
    #ifdef LOG_OUT
            if(dis2_count > 10)
            {
                // const uint32_t total_price = (uint32_t)((((double)cap_data_dis2.unit_price * (double)cap_data_dis2.volume_l)/1000.0)+0.5);
                ESP_LOGI(TAG, "dis2\t%ld\t%ld\t%ld\r\n", cap_data_dis2.unit_price, cap_data_dis2.total_price, cap_data_dis2.volume_l);
                dis2_count = 0;
            }
            else
            {
                dis2_count++;
            }
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


static bool get_display_data(dis_capture_t *dd, uint8_t *ab)
{
    bool error = false;
    dis_capture_t data = {};

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
    memcpy((void *)dd, (void *)&data, sizeof(dis_capture_t));

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