#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "dis_common.h"

#if CONFIG_LOG_MAXIMUM_LEVEL > 2
    // #define LOG_OUT
#endif

#define PACKET_SIZE 14
#define PACKET_DELAY_IDX 7
#define PACKET_SKIP_RCLK_LOW_ID 0xD
#define PACKET_DELAY_IDX_LL 0xA

#define set_dis1_sdata1(level) gpio_set_level(DIS1_OUT_DATA1, level)
#define set_dis1_sdata2(level) gpio_set_level(DIS1_OUT_DATA2, level)
#define set_dis1_sclk(level) gpio_set_level(DIS1_OUT_SCLK, level)
#define set_dis1_rclk(level) gpio_set_level(DIS1_OUT_RCLK, level)

#define set_dis2_sdata1(level) gpio_set_level(DIS2_OUT_DATA1, level)
#define set_dis2_sdata2(level) gpio_set_level(DIS2_OUT_DATA2, level)
#define set_dis2_sclk(level) gpio_set_level(DIS2_OUT_SCLK, level)
#define set_dis2_rclk(level) gpio_set_level(DIS2_OUT_RCLK, level)

#define delayMicroseconds(u_sec)                                   \
    {                                                              \
        const uint64_t us_now = esp_timer_get_time();              \
        while (((uint64_t)esp_timer_get_time()) <= us_now + u_sec) \
        {                                                          \
            portYIELD();                                           \
        }                                                          \
    }

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
    IDX_SELECT_L = 0,

    IDX_TOTAL_START = 0,
    IDX_TOTAL_7 = IDX_TOTAL_START,
    IDX_TOTAL_6,
    IDX_TOTAL_5,
    IDX_TOTAL_4,
    IDX_TOTAL_3,
    IDX_TOTAL_2,
    IDX_TOTAL_1,
    IDX_TOTAL_0,
    IDX_TOTAL_SIZE,

    IDX_UNIT_START = 8,
    IDX_UNIT_5 = IDX_UNIT_START,
    IDX_UNIT_4,
    IDX_UNIT_3,
    IDX_UNIT_2,
    IDX_UNIT_1,
    IDX_UNIT_0,
    IDX_UNIT_SIZE
} ab_byte_index_t;

typedef enum
{
    IDX_SELECT_P = 0,

    IDX_LITERS_START = 1,
    IDX_LITERS_6 = IDX_LITERS_START,
    IDX_LITERS_5,
    IDX_LITERS_4,
    IDX_LITERS_3,
    IDX_LITERS_2,
    IDX_LITERS_1,
    IDX_LITERS_0,
    IDX_LITERS_SIZE,

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
    LL_IDX_TOT_LITERS_6 = IDX_LITERS_0,
    LL_IDX_TOT_LITERS_7 = IDX_LITERS_1,
    LL_IDX_TOT_LITERS_8 = IDX_LITERS_2,
    LL_IDX_TOT_LITERS_9 = IDX_LITERS_3,
    LL_IDX_TOT_LITERS_10 = IDX_LITERS_4,
    LL_IDX_TOT_LITERS_11 = IDX_LITERS_5,
    LL_IDX_TOT_LITERS_12 = IDX_LITERS_6,
} bb_ll_byte_index_t;

typedef union
{
    struct
    {
        uint8_t IDX : 4; // first 4 bits
        uint8_t DIG : 4; // last 4 bits
    };
    uint8_t u8int;
} data_t;

data_t dis1_sdata1_tx[PACKET_SIZE] = {
    {{.IDX = 0x0, .DIG = 0xF}},
    {{.IDX = 0x1, .DIG = 0xF}},
    {{.IDX = 0x2, .DIG = 0x3}},
    {{.IDX = 0x3, .DIG = 0x7}},
    {{.IDX = 0x4, .DIG = 0x9}},
    {{.IDX = 0x5, .DIG = 0x0}},
    {{.IDX = 0x6, .DIG = 0x0}},
    {{.IDX = 0x7, .DIG = 0x0}},
    {{.IDX = 0x8, .DIG = 0xF}},
    {{.IDX = 0x9, .DIG = 0x3}},
    {{.IDX = 0xA, .DIG = 0x7}},
    {{.IDX = 0xB, .DIG = 0x9}},
    {{.IDX = 0xC, .DIG = 0x0}},
    {{.IDX = 0xD, .DIG = 0x0}}};
data_t dis1_sdata2_tx[PACKET_SIZE] = {
    {{.IDX = 0x0, .DIG = 0xF}},
    {{.IDX = 0x1, .DIG = 0xF}},
    {{.IDX = 0x2, .DIG = 0xF}},
    {{.IDX = 0x3, .DIG = 0x1}},
    {{.IDX = 0x4, .DIG = 0x0}},
    {{.IDX = 0x5, .DIG = 0x0}},
    {{.IDX = 0x6, .DIG = 0x0}},
    {{.IDX = 0x7, .DIG = 0x0}},
    {{.IDX = 0x8, .DIG = 0xF}},
    {{.IDX = 0x9, .DIG = 0xF}},
    {{.IDX = 0xA, .DIG = 0xF}},
    {{.IDX = 0xB, .DIG = 0xF}},
    {{.IDX = 0xC, .DIG = 0xF}},
    {{.IDX = 0xD, .DIG = 0xF}}};

#if DIS2_CAPTURE_ENABLE
data_t dis2_sdata1_tx[PACKET_SIZE] = {
    {{.IDX = 0x0, .DIG = 0xF}},
    {{.IDX = 0x1, .DIG = 0xF}},
    {{.IDX = 0x2, .DIG = 0x3}},
    {{.IDX = 0x3, .DIG = 0x7}},
    {{.IDX = 0x4, .DIG = 0x9}},
    {{.IDX = 0x5, .DIG = 0x0}},
    {{.IDX = 0x6, .DIG = 0x0}},
    {{.IDX = 0x7, .DIG = 0x0}},
    {{.IDX = 0x8, .DIG = 0xF}},
    {{.IDX = 0x9, .DIG = 0x3}},
    {{.IDX = 0xA, .DIG = 0x7}},
    {{.IDX = 0xB, .DIG = 0x9}},
    {{.IDX = 0xC, .DIG = 0x0}},
    {{.IDX = 0xD, .DIG = 0x0}}};
data_t dis2_sdata2_tx[PACKET_SIZE] = {
    {{.IDX = 0x0, .DIG = 0xF}},
    {{.IDX = 0x1, .DIG = 0xF}},
    {{.IDX = 0x2, .DIG = 0xF}},
    {{.IDX = 0x3, .DIG = 0x1}},
    {{.IDX = 0x4, .DIG = 0x0}},
    {{.IDX = 0x5, .DIG = 0x0}},
    {{.IDX = 0x6, .DIG = 0x0}},
    {{.IDX = 0x7, .DIG = 0x0}},
    {{.IDX = 0x8, .DIG = 0xF}},
    {{.IDX = 0x9, .DIG = 0xF}},
    {{.IDX = 0xA, .DIG = 0xF}},
    {{.IDX = 0xB, .DIG = 0xF}},
    {{.IDX = 0xC, .DIG = 0xF}},
    {{.IDX = 0xD, .DIG = 0xF}}};
#endif

static dis_capture_t dis1_data = {0}; //{.volume_l = 1000, .unit_price = 30000, .total_price = 30000};
static dis_capture_t dis1_data_temp = {0};
static SemaphoreHandle_t dis1_data_copy = NULL;
#if DIS2_CAPTURE_ENABLE
static dis_capture_t dis2_data = {0}; //{.volume_l = 1000, .unit_price = 40000, .total_price = 40000};
static dis_capture_t dis2_data_temp = {0};
static SemaphoreHandle_t dis2_data_copy = NULL;
#endif

static const char *TAG = "hngy_display";

static void dis1_send_byte(uint8_t byte1, uint8_t byte2, bool skip_rclk_low);
static void task_dis1_hongyang8(void *arg);
#if DIS2_CAPTURE_ENABLE
static void dis2_send_byte(uint8_t byte1, uint8_t byte2, bool skip_rclk_low);
static void task_dis2_hongyang8(void *arg);
#endif
static void create_buffer_hongyang_8(data_t *sdata1, data_t *sdata2, uint32_t unit_001, uint32_t total_001, uint32_t volume_0001);
static void create_buffer_hongyang_8_ll(data_t *sdata1, data_t *sdata2, uint64_t total_ll);

void dis1_copy_data(const dis_capture_t *dis)
{
    if (xSemaphoreTake(dis1_data_copy, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        memcpy(&dis1_data_temp, dis, sizeof(dis_capture_t));
        xSemaphoreGive(dis1_data_copy);
    }
}

#if DIS2_CAPTURE_ENABLE
void dis2_copy_data(const dis_capture_t *dis)
{
    if (xSemaphoreTake(dis2_data_copy, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        memcpy(&dis2_data_temp, dis, sizeof(dis_capture_t));
        xSemaphoreGive(dis2_data_copy);
    }
}
#endif

void init_hongyang_display()
{
    // config Hongyang display outputs
    ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(DIS1_OUT_RCLK) | BIT64(DIS1_OUT_SCLK) | BIT64(DIS1_OUT_DATA1) | BIT64(DIS1_OUT_DATA2)
#if DIS2_CAPTURE_ENABLE
                        | BIT64(DIS2_OUT_RCLK) | BIT64(DIS2_OUT_SCLK) | BIT64(DIS2_OUT_DATA1) | BIT64(DIS2_OUT_DATA2)
#endif
        ,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE}));

//     ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
//         .intr_type = GPIO_INTR_DISABLE,
//         .mode = GPIO_MODE_OUTPUT,
//         .pin_bit_mask = BIT64(DIS1_OUT_SCLK),
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .pull_up_en = GPIO_PULLUP_DISABLE}));

//     ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
//         .intr_type = GPIO_INTR_DISABLE,
//         .mode = GPIO_MODE_OUTPUT,
//         .pin_bit_mask = BIT64(DIS1_OUT_DATA1),
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .pull_up_en = GPIO_PULLUP_DISABLE}));

    // ESP_ERROR_CHECK(gpio_config(&(const gpio_config_t){
    //     .intr_type = GPIO_INTR_DISABLE,
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = BIT64(DIS1_OUT_DATA2),
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en = GPIO_PULLUP_DISABLE}));

    dis1_data_copy = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(dis1_data_copy == NULL);
    ESP_ERROR_CHECK(xTaskCreate(task_dis1_hongyang8, "task_dis1_hongyang8", 4 * 1024, NULL, 8, NULL) == pdFALSE);
#if DIS2_CAPTURE_ENABLE
    dis2_data_copy = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(dis2_data_copy == NULL);
    ESP_ERROR_CHECK(xTaskCreate(task_dis2_hongyang8, "task_dis2_hongyang8", 4 * 1024, NULL, 8, NULL) == pdFALSE);
#endif
    ESP_LOGI(TAG, "Done");
}

static void task_dis1_hongyang8(void *arg)
{
    xSemaphoreGive(dis1_data_copy);
    ESP_LOGI(TAG, "Starting display 1 output task");
    while (1)
    {
        xSemaphoreTake(dis1_data_copy, portMAX_DELAY);
        memcpy(&dis1_data, &dis1_data_temp, sizeof(dis1_data));
        xSemaphoreGive(dis1_data_copy);
        if(dis1_data.flags.select_ll)
            create_buffer_hongyang_8_ll(dis1_sdata1_tx, dis1_sdata2_tx, dis1_data.total_liters);
        else
            create_buffer_hongyang_8(dis1_sdata1_tx, dis1_sdata2_tx, dis1_data.unit_price, dis1_data.total_price, dis1_data.volume_l);

        for (size_t i = 0; i < PACKET_SIZE; i++)
        {
            if (i == PACKET_DELAY_IDX) // add 7ms delay at end of packet ID 7
            {
                dis1_send_byte(dis1_sdata1_tx[i].u8int, dis1_sdata2_tx[i].u8int, true);
                delayMicroseconds(246);
            }
            else if (i == PACKET_DELAY_IDX_LL) // add 348us delay at end packet ID 0xA
            {
                dis1_send_byte(dis1_sdata1_tx[i].u8int, dis1_sdata2_tx[i].u8int, true);
                delayMicroseconds(1863);
            }
            else
            {
                dis1_send_byte(dis1_sdata1_tx[i].u8int, dis1_sdata2_tx[i].u8int, false);
            }
        }
#ifdef LOG_OUT
        if(dis1_data.flags.select_ll)
            ESP_LOGI(TAG, "dis1\ttotalizer=%lld", dis1_data.total_liters);
        else
            ESP_LOGI(TAG, "dis1\tunit=%.2f\ttotal=%.2f\tvolume=%.3f", dis1_data.unit_price / 100.0, dis1_data.total_price / 100.0, dis1_data.volume_l / 1000.0);
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static void dis1_send_byte(uint8_t byte1, uint8_t byte2, bool skip_rclk_low)
{
    const uint8_t bit_map[] = {
        0b10000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000100,
        0b00000010,
        0b00000001};

    set_dis1_rclk(false);

    for (int i = 0; i < 8; i++)
    {
        const bool data1 = (bool)(byte1 & bit_map[i]);
        const bool data2 = (bool)(byte2 & bit_map[i]);

        set_dis1_sdata1(data1);
        set_dis1_sdata2(data2);
        delayMicroseconds(1);
        set_dis1_sclk(true);
        delayMicroseconds(1);
        set_dis1_sclk(false);
        delayMicroseconds(10);
    }

    set_dis1_sdata1(false);
    set_dis1_sdata2(false);
    set_dis1_rclk(true);
    delayMicroseconds(9);
    if (!skip_rclk_low)
        set_dis1_rclk(false);
    delayMicroseconds(5);
}

#if DIS2_CAPTURE_ENABLE
static void task_dis2_hongyang8(void *arg)
{
    xSemaphoreGive(dis2_data_copy);
    ESP_LOGI(TAG, "Starting display 2 output task");
    while (1)
    {
        xSemaphoreTake(dis2_data_copy, portMAX_DELAY);
        memcpy(&dis2_data, &dis2_data_temp, sizeof(dis2_data));
        xSemaphoreGive(dis2_data_copy);
        if(dis2_data.flags.select_ll)
            create_buffer_hongyang_8_ll(dis2_sdata1_tx, dis2_sdata2_tx, dis2_data.total_liters);
        else
            create_buffer_hongyang_8(dis2_sdata1_tx, dis2_sdata2_tx, dis2_data.unit_price, dis2_data.total_price, dis2_data.volume_l);

        for (size_t i = 0; i < PACKET_SIZE; i++)
        {
            if (i == PACKET_DELAY_IDX) // add 7ms delay at end of packet ID 7
            {
                dis2_send_byte(dis2_sdata1_tx[i].u8int, dis2_sdata2_tx[i].u8int, true);
                delayMicroseconds(246);
            }
            else if (i == PACKET_DELAY_IDX_LL) // add 348us delay at end packet ID 0xA
            {
                dis2_send_byte(dis2_sdata1_tx[i].u8int, dis2_sdata2_tx[i].u8int, true);
                delayMicroseconds(1863);
            }
            else
            {
                dis2_send_byte(dis2_sdata1_tx[i].u8int, dis2_sdata2_tx[i].u8int, false);
            }
        }
#ifdef LOG_OUT
        if(dis2_data.flags.select_ll)
            ESP_LOGI(TAG, "dis2\ttotalizer=%lld", dis2_data.total_liters);
        else
            ESP_LOGI(TAG, "dis2\tunit=%.2f\ttotal=%.2f\tvolume=%.3f", dis2_data.unit_price / 100.0, dis2_data.total_price / 100.0, dis2_data.volume_l / 1000.0);
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static void dis2_send_byte(uint8_t byte1, uint8_t byte2, bool skip_rclk_low)
{
    const uint8_t bit_map[] = {
        0b10000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000100,
        0b00000010,
        0b00000001};

    set_dis2_rclk(false);

    for (int i = 0; i < 8; i++)
    {
        const bool data1 = (bool)(byte1 & bit_map[i]);
        const bool data2 = (bool)(byte2 & bit_map[i]);

        set_dis2_sdata1(data1);
        set_dis2_sdata2(data2);
        delayMicroseconds(1);
        set_dis2_sclk(true);
        delayMicroseconds(1);
        set_dis2_sclk(false);
        delayMicroseconds(10);
    }

    set_dis2_sdata1(false);
    set_dis2_sdata2(false);
    set_dis2_rclk(true);
    delayMicroseconds(9);
    if (!skip_rclk_low)
        set_dis2_rclk(false);
    delayMicroseconds(5);
}
#endif

static void create_buffer_hongyang_8(data_t *sdata1, data_t *sdata2, uint32_t unit_001, uint32_t total_001, uint32_t volume_0001)
{
    // load unit price 0.01
    for (int i = IDX_UNIT_0; i >= IDX_UNIT_START; i--)
    {
        if (unit_001)
        {
            sdata1[i].DIG = unit_001 % 10;
            unit_001 /= 10;
        }
        else if (i == IDX_UNIT_0)
        {
            sdata1[i].DIG = 0x0;
        }
        else
        {
            sdata1[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load total price 0.01
    for (int i = IDX_TOTAL_0; i >= IDX_TOTAL_START; i--)
    {
        if (total_001)
        {
            sdata1[i].DIG = total_001 % 10;
            total_001 /= 10;
        }
        else if (i == IDX_TOTAL_0)
        {
            sdata1[i].DIG = 0x0;
        }
        else
        {
            sdata1[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load liters 0.001
    for (int i = IDX_LITERS_0; i >= IDX_LITERS_START; i--)
    {
        if (volume_0001)
        {
            sdata2[i].DIG = volume_0001 % 10;
            volume_0001 /= 10;
        }
        else if (i == IDX_LITERS_0)
        {
            sdata2[i].DIG = 0x0;
        }
        else
        {
            sdata2[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}

static void create_buffer_hongyang_8_ll(data_t *sdata1, data_t *sdata2, uint64_t total_ll)
{
    // set Totaliser detect charactors
    sdata1[LL_IDX_LL_1].DIG = DIS_CHARACTOR_L;
    sdata1[LL_IDX_LL_2].DIG = DIS_CHARACTOR_L;
    // clear rest of digits zero
    for (size_t i = IDX_TOTAL_5; i >= IDX_TOTAL_0; i--)
    {
        sdata1[i].DIG = DIS_CHARACTOR_BLANK;
    }
    
    // load unit price 0.01
    for (int i = IDX_UNIT_0; i >= IDX_UNIT_START; i--)
    {
        if (total_ll)
        {
            sdata1[i].DIG = total_ll % 10;
            total_ll /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata1[i].DIG = 0x0;
        }
        else
        {
            sdata1[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load digits for liters section 0.001
    for (int i = IDX_LITERS_0; i >= IDX_LITERS_6; i--)
    {
        if (total_ll)
        {
            sdata2[i].DIG = total_ll % 10;
            total_ll /= 10;
        }
        else if(i == IDX_LITERS_0)
        {
            sdata2[i].DIG = 0x0;
        }
        else
        {
            sdata2[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}