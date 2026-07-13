#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "settings.h"
#include "wayn_6_digit.h"

// convert price into display counts
#define PRICE_TO_DIS_COUNTS(x) (x * 10) //in Wayne 6 digit, only one decimal point is showing.
#define VOLUME_COUNTS 100 //in Wayne 6 digit, only have two decimal digits

#define DATA_SET_SIZE 16 //actual data size to consider display digits
#define DATA_PACKET_SIZE 45 //bytes gets for a single timeout
#define RX_BUF_SIZE DATA_PACKET_SIZE * 2 //temprory store size
#define DROP_COUNT 8 //packet drop count

#define DIS_ENB GPIO_NUM_15

#define D1_SCLK GPIO_NUM_14
#define D1_RCLK GPIO_NUM_12
#define D1_SDATA1 GPIO_NUM_13
#define D1_SDATA2 GPIO_NUM_16

#define D2_SCLK GPIO_NUM_4
#define D2_RCLK GPIO_NUM_5
#define D2_SDATA1 GPIO_NUM_2
#define D2_SDATA2 GPIO_NUM_0

#define c_pin_rclk_read_dis_1 (bool)((GPIO.in >> D1_RCLK) & 0x1)
#define c_pin_sdata1_read_dis_1 (bool)((GPIO.in >> D1_SDATA1) & 0x1)

#define c_pin_rclk_read_dis_2 (bool)((GPIO.in >> D2_RCLK) & 0x1)
#define c_pin_sdata1_read_dis_2 (bool)((GPIO.in >> D2_SDATA1) & 0x1)

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
    
    bits 7, 6, 5, 4, 3, 2, 1, 0
         A, B, C, D, E, F, G, H
*/
#define CODE_DIGIT_MAP(XX) \
    XX(0, 0xFC, 0xFD, 0)   \
    XX(1, 0x60, 0x61, 1)   \
    XX(2, 0xDA, 0xDB, 2)   \
    XX(3, 0xF2, 0xF3, 3)   \
    XX(4, 0x66, 0x67, 4)   \
    XX(5, 0xB6, 0xB7, 5)   \
    XX(6, 0xBE, 0xBF, 6)   \
    XX(7, 0xE0, 0xE1, 7)   \
    XX(8, 0xFE, 0xFF, 8)   \
    XX(9, 0xF6, 0xF7, 9)   \
    XX(L, 0x1C, 0x1D, 0)   \
    XX(P, 0xCE, 0xCF, 0)   \
    XX(A, 0xEE, 0xEF, 0)   \
    XX(n, 0x2A, 0x2B, 0)

typedef enum
{
    IDX_VOLUME_START = 0,
    IDX_VOLUME_5 = IDX_VOLUME_START,
    IDX_VOLUME_4,
    IDX_VOLUME_3,
    IDX_VOLUME_2,
    IDX_VOLUME_1,
    IDX_VOLUME_0,
    IDX_VOLUME_SIZE,

    IDX_TOTAL_START = 6,
    IDX_TOTAL_5 = IDX_TOTAL_START,
    IDX_TOTAL_4,
    IDX_TOTAL_3,
    IDX_TOTAL_2,
    IDX_TOTAL_1,
    IDX_TOTAL_0,
    IDX_TOTAL_SIZE,

    IDX_UNIT_START = 12,
    IDX_UNIT_3 = IDX_UNIT_START,
    IDX_UNIT_2,
    IDX_UNIT_1,
    IDX_UNIT_0,
    IDX_UNIT_SIZE,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L,
} byte_index_t;

typedef enum
{
    LL_IDX_LL_A = IDX_UNIT_3,
    LL_IDX_LL_n = IDX_UNIT_1,
    LL_IDX_LL_1 = IDX_UNIT_0,

    LL_IDX_TOT_LITERS_0  = IDX_VOLUME_0,
    LL_IDX_TOT_LITERS_1  = IDX_VOLUME_1,
    LL_IDX_TOT_LITERS_2  = IDX_VOLUME_2,
    LL_IDX_TOT_LITERS_3  = IDX_VOLUME_3,
    LL_IDX_TOT_LITERS_4  = IDX_VOLUME_4,
    LL_IDX_TOT_LITERS_5  = IDX_VOLUME_5,

    LL_IDX_TOT_LITERS_6  = IDX_TOTAL_0,
    LL_IDX_TOT_LITERS_7  = IDX_TOTAL_1,
    LL_IDX_TOT_LITERS_8  = IDX_TOTAL_2,
    LL_IDX_TOT_LITERS_9  = IDX_TOTAL_3,
    LL_IDX_TOT_LITERS_10 = IDX_TOTAL_4,
    LL_IDX_TOT_LITERS_11 = IDX_TOTAL_5,
} ll_byte_index_t;

typedef enum
{
#define XX(NAME, SEG, SEG_DECI, VALUE) DIS_CHAR_##NAME = SEG,
    CODE_DIGIT_MAP(XX)
#undef XX
} code_digit_t;

static const uint8_t seg7_to_digit[256] = {
#define XX(NAME, SEG, SEG_DECI, VALUE) [SEG] = VALUE, [SEG_DECI] = VALUE,
    CODE_DIGIT_MAP(XX)
#undef XX
};

typedef struct
{
    uint8_t id;
    uint8_t ab[DATA_SET_SIZE];
} capture_data_t;

static xQueueHandle capture_queue = NULL;
static xQueueHandle *ptr_send_que = NULL;

// display tap 1
static esp_timer_handle_t ptimer_dis_1 = NULL;
static uint8_t byte1_display_1 = 0, bit_idx_display_1 = 0;
static uint8_t data1_display_1[RX_BUF_SIZE] = {};
static size_t rx_index_display_1 = 0;
static capture_data_t capture_display_1 = {.id = TX_ID_DIS1_DATA};
static TickType_t pckt_ticks_display_1 = 0; // keep time track for last packet
// display tap 2
static esp_timer_handle_t ptimer_dis_2 = NULL;
static uint8_t byte1_display_2 = 0, bit_idx_display_2 = 0;
static uint8_t data1_display_2[RX_BUF_SIZE] = {};
static uint32_t rx_index_display_2 = 0;
static capture_data_t capture_display_2 = {.id = TX_ID_DIS2_DATA};
static TickType_t pckt_ticks_display_2 = 0; // keep time track for last packet

static void IRAM_ATTR bit_read_dis_1(void *arg) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
    if (c_pin_rclk_read_dis_1) // skip bit reading if RCLK is HIGH
        return;

    byte1_display_1 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_1) | (uint8_t)(byte1_display_1 << 1));
    bit_idx_display_1++;
    if (bit_idx_display_1 > 7)
    {
        data1_display_1[rx_index_display_1] = byte1_display_1;
        bit_idx_display_1 = 0;
        byte1_display_1 = 0;
        rx_index_display_1++;
        if (!(rx_index_display_1 < RX_BUF_SIZE)) // if buffer exceeding, start from begining
            rx_index_display_1 = 0;
    }
}
static void IRAM_ATTR byte_read_start_dis_1(void *arg) // restart timer on every chip select start edge (-ve edge)
{
    esp_timer_start_once(ptimer_dis_1, 20 * 1000);
}

static void IRAM_ATTR bit_read_dis_2(void *arg) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
    if (c_pin_rclk_read_dis_2) // skip bit reading if RCLK is HIGH
        return;

    byte1_display_2 = (uint8_t)((uint8_t)(c_pin_sdata1_read_dis_2) | (uint8_t)(byte1_display_2 << 1));
    bit_idx_display_2++;
    if (bit_idx_display_2 > 7)
    {
        bit_idx_display_2 = 0;
        data1_display_2[rx_index_display_2] = byte1_display_2;
        byte1_display_2 = 0;
        rx_index_display_2++;
        if (!(rx_index_display_2 < RX_BUF_SIZE)) // if buffer exceeding, start from begining
            rx_index_display_2 = 0;
    }
}

static void IRAM_ATTR byte_read_start_dis_2(void *arg) // restart timer on every chip select start edge (-ve edge)
{
    esp_timer_start_once(ptimer_dis_2, 20 * 1000);
}

static void IRAM_ATTR data_rx_timeout_dis_1(void *arg)
{
    const uint32_t biff_size = rx_index_display_1;
    bit_idx_display_1 = 0;
    rx_index_display_1 = 0;
    byte1_display_1 = 0;
    if (biff_size < DATA_SET_SIZE) //drop packet if not enough display bytes
    {
        return;
    }
    const TickType_t ticks_now = xTaskGetTickCountFromISR();
    if((ticks_now - pckt_ticks_display_1) > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) && memcmp(capture_display_1.ab, data1_display_1, sizeof(capture_display_1.ab)) != 0)
    {
        memcpy(capture_display_1.ab, data1_display_1, sizeof(uint8_t) * DATA_SET_SIZE);
        xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
        pckt_ticks_display_1 = ticks_now;
    }
    else if((ticks_now - pckt_ticks_display_1) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
    {
        xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
        pckt_ticks_display_1 = ticks_now;
    }
}

static void IRAM_ATTR data_rx_timeout_dis_2(void *arg)
{
    const uint32_t biff_size = rx_index_display_2;
    bit_idx_display_2 = 0;
    rx_index_display_2 = 0;
    byte1_display_2 = 0;
    if (biff_size < DATA_SET_SIZE)
    {
        return;
    }
    const TickType_t ticks_now = xTaskGetTickCountFromISR();
    if((ticks_now - pckt_ticks_display_2) > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) && memcmp(capture_display_2.ab, data1_display_2, sizeof(uint8_t) * DATA_SET_SIZE) != 0)
    {
        memcpy(capture_display_2.ab, data1_display_2, sizeof(uint8_t) * DATA_SET_SIZE);
        xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
        pckt_ticks_display_2 = ticks_now;
    }
    else if((ticks_now - pckt_ticks_display_2) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
    {
        xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
        pckt_ticks_display_2 = ticks_now;
    }
}

/**
 * 
*/
static void get_display_data(wyn_6_digit_t *dd, uint8_t *ab)
{
    wyn_6_digit_t data = {};

    /* Check Select LL, L, Select P*/
    data.flags.select_ll = (bool)(ab[LL_IDX_LL_A] == DIS_CHAR_A || ab[LL_IDX_LL_n] == DIS_CHAR_n);
    data.flags.select_l = (bool)((uint8_t)(ab[IDX_SELECT_L]) == DIS_CHAR_L);
    data.flags.select_p = (bool)((uint8_t)(ab[IDX_SELECT_P]) == DIS_CHAR_P);

    /* check digit matching for volume */
    for (size_t i = IDX_VOLUME_START; i < IDX_VOLUME_SIZE; i++)
    {
        ab[i] = seg7_to_digit[ab[i]];
    }
    /* check digit matching for total price */
    for (size_t i = IDX_TOTAL_START; i < IDX_TOTAL_SIZE; i++)
    {
        ab[i] = seg7_to_digit[ab[i]];
    }
    /* check digit matching for total price */
    for (size_t i = IDX_UNIT_START; i < IDX_UNIT_SIZE; i++)
    {
        ab[i] = seg7_to_digit[ab[i]];
    }

    /*if Total Liter option is selected, calculate it*/
    if (data.flags.select_ll)
    {
        data.total_liters = (uint64_t)(ab[LL_IDX_TOT_LITERS_11]) * 100000000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_10]) * 10000000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_9])  * 1000000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_8])  * 100000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_7])  * 10000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_6])  * 1000000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_5])  * 100000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_4])  * 10000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_3])  * 1000 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_2])  * 100 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_1])  * 10 +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_0]);
    }
    else
    {
        data.unit_price = (uint32_t)(ab[IDX_UNIT_3]) * 1000 +
                          (uint32_t)(ab[IDX_UNIT_2]) * 100 +
                          (uint32_t)(ab[IDX_UNIT_1]) * 10 +
                          (uint32_t)(ab[IDX_UNIT_0]);

        data.total_price = (uint32_t)(ab[IDX_TOTAL_5]) * 100000 +
                           (uint32_t)(ab[IDX_TOTAL_4]) * 10000 +
                           (uint32_t)(ab[IDX_TOTAL_3]) * 1000 +
                           (uint32_t)(ab[IDX_TOTAL_2]) * 100 +
                           (uint32_t)(ab[IDX_TOTAL_1]) * 10 +
                           (uint32_t)(ab[IDX_TOTAL_0]);

        data.volume_l = (uint32_t)(ab[IDX_VOLUME_5]) * 100000 +
                        (uint32_t)(ab[IDX_VOLUME_4]) * 10000 +
                        (uint32_t)(ab[IDX_VOLUME_3]) * 1000 +
                        (uint32_t)(ab[IDX_VOLUME_2]) * 100 +
                        (uint32_t)(ab[IDX_VOLUME_1]) * 10 +
                        (uint32_t)(ab[IDX_VOLUME_0]);

        //decimal point of total price and unit price is equal, so consider volume decimal points only
		uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
		gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
		if (gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR)) // if total price should match with unit_price*valume. Check for the tolerance
		{
			data.error.err_bit.price_gap = true;
		}
    }
    memcpy((void *)dd, (void *)&data, sizeof(wyn_6_digit_t));
}

static void data_send_task(void *arg)
{
    capture_data_t capture = {};
    data_packet_t display_data = {
        .display = DIS_WAYNE_6_DIGIT,
        .length = sizeof(wyn_6_digit_t)};
    while (1)
    {
        if (xQueueReceive(capture_queue, &capture, pdMS_TO_TICKS(10) /*portMAX_DELAY*/))
        {
            display_data.pck_id = capture.id;
            get_display_data((wyn_6_digit_t *)(display_data.ab_data), capture.ab);
            if((((wyn_6_digit_t *)(display_data.ab_data))->error.u8int & settings.error_mask.u8int) == 0) //if error flag is non zero, do not send
                xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10)); // if queue is full, wait 10ms until it gets clear
            // const wyn_6_digit_t *dis = (wyn_6_digit_t *)(display_data.ab_data);
            // printf("unit=%d total=%d volume=%d\r\n", dis->unit_price, dis->total_price, dis->volume_l);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t display_wayne_6_digit_init(xQueueHandle *send_que)
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
    io_conf.pin_bit_mask = BIT(D1_SCLK) | BIT(D2_SCLK);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set two clock negative edge intterupt pin inputs
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT(D1_RCLK) | BIT(D2_RCLK);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set DATA1 and DATA2 bits reading inputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT(D1_SDATA1) | BIT(D2_SDATA1);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set unused DATA2 inputs and PULL UPs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT(D1_SDATA2) | BIT(D2_SDATA2);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // install gpio isr service
    gpio_install_isr_service(0);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(D1_SCLK, bit_read_dis_1, NULL);
    gpio_isr_handler_add(D1_RCLK, byte_read_start_dis_1, NULL);
    gpio_isr_handler_add(D2_SCLK, bit_read_dis_2, NULL);
    gpio_isr_handler_add(D2_RCLK, byte_read_start_dis_2, NULL);

    capture_queue = xQueueCreate(20, sizeof(capture_data_t));
    if (capture_queue == NULL)
    {
        return ESP_FAIL;
    }

    ptr_send_que = send_que;

    esp_timer_create(&(const esp_timer_create_args_t){
                         .callback = data_rx_timeout_dis_1,
                         .name = "ptimer_dis_1"},
                     &ptimer_dis_1);
    esp_timer_create(&(const esp_timer_create_args_t){
                         .callback = data_rx_timeout_dis_2,
                         .name = "ptimer_dis_2"},
                     &ptimer_dis_2);

    bit_idx_display_1 = 0;
    rx_index_display_1 = 0;
    byte1_display_1 = 0;
    bit_idx_display_1 = 0;
    rx_index_display_2 = 0;
    byte1_display_2 = 0;

    if (xTaskCreate(data_send_task, "data_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        return ESP_FAIL;
    }
    gpio_set_level(DIS_ENB, true);

    // printf("Staring Wayne display\r\n");
    return ESP_OK;
}
