#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "settings.h"
#include "longfeng_8_digit.h"

// convert price into display counts
#define PRICE_TO_DIS_COUNTS(x) (x * 10) // in Longfeng 8 digit, two decimal point is showing.
#define VOLUME_COUNTS 1000ULL           // in Longfeng 8 digit, have tree decimal digits

#define DATA_SET_SIZE 24

#define DIS_ENB GPIO_NUM_15

#define D1_SCLK GPIO_NUM_14
#define D1_RCLK GPIO_NUM_12
#define D1_SDATA1 GPIO_NUM_13

#define D2_SCLK GPIO_NUM_4
#define D2_RCLK GPIO_NUM_5
#define D2_SDATA1 GPIO_NUM_2

#define D1_SDATA2 GPIO_NUM_16
#define D2_SDATA2 GPIO_NUM_0

#define c_pin_sdata1_read_dis_1 (bool)((GPIO.in >> D1_SDATA1) & 0x1)
#define c_pin_sdata1_read_dis_2 (bool)((GPIO.in >> D1_SDATA2) & 0x1)

#define c_pin_rclk_read_dis_1 (bool)((GPIO.in >> D1_RCLK) & 0x1)
#define c_pin_rclk_read_dis_2 (bool)((GPIO.in >> D2_RCLK) & 0x1)

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

// This pump doesn't give totaliser on display, only show in keypad display

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
static uint8_t bit_idx_display_1 = 0;
static uint8_t bits1_display_1 = 0;
static capture_data_t capture_display_1 = {.id = TX_ID_DIS1_DATA};

// display tap 2
static uint8_t bit_idx_display_2 = 0;
static uint8_t bits1_display_2 = 0;
static capture_data_t capture_display_2 = {.id = TX_ID_DIS1_DATA};

static void pin_interrupt_enable_dis_1();
static void pin_interrupt_enable_dis_2();
static void pin_interrupt_disable_dis_1();
static void pin_interrupt_disable_dis_2();

static void IRAM_ATTR bit_read_dis_1(void *arg) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
    bits1_display_1 = (uint32_t)((uint32_t)(c_pin_sdata1_read_dis_1) | (uint32_t)(bits1_display_1 << 1));
    switch (bit_idx_display_1)
    {
        // case 0:
        //     capture_display_1.ab[0] = bits1_display_1;
        //     break;
        // case 1:
        //     capture_display_1.ab[1] = bits1_display_1;
        //     break;
        // case 2:
        //     capture_display_1.ab[2] = bits1_display_1;
        //     break;
        // case 3:
        //     capture_display_1.ab[3] = bits1_display_1;
        //     break;
        // case 4:
        //     capture_display_1.ab[4] = bits1_display_1;
        //     break;
        // case 5:
        //     capture_display_1.ab[5] = bits1_display_1;
        //     break;
        // case 6:
        //     capture_display_1.ab[6] = bits1_display_1;
        //     break;
        // case 7:
        //     capture_display_1.ab[7] = bits1_display_1;
        //     break;

    case 8:
        capture_display_1.ab[0] = bits1_display_1;
        break;
    case 16:
        capture_display_1.ab[1] = bits1_display_1;
        break;
    case 24:
        capture_display_1.ab[2] = bits1_display_1;
        break;
    case 32:
        capture_display_1.ab[3] = bits1_display_1;
        break;
    case 40:
        capture_display_1.ab[4] = bits1_display_1;
        break;
    case 48:
        capture_display_1.ab[5] = bits1_display_1;
        break;
    case 56:
        capture_display_1.ab[6] = bits1_display_1;
        break;
    case 64:
        capture_display_1.ab[7] = bits1_display_1;
        break;
    case 72:
        capture_display_1.ab[8] = bits1_display_1;
        break;
    case 80:
        capture_display_1.ab[9] = bits1_display_1;
        break;
    case 88:
        capture_display_1.ab[10] = bits1_display_1;
        break;
    case 96:
        capture_display_1.ab[11] = bits1_display_1;
        break;
    case 104:
        capture_display_1.ab[12] = bits1_display_1;
        break;
    case 112:
        capture_display_1.ab[13] = bits1_display_1;
        break;
    case 120:
        capture_display_1.ab[14] = bits1_display_1;
        break;
    case 128:
        capture_display_1.ab[15] = bits1_display_1;
        break;
    case 136:
        capture_display_1.ab[16] = bits1_display_1;
        break;
    case 144:
        capture_display_1.ab[17] = bits1_display_1;
        break;
    case 152:
        capture_display_1.ab[18] = bits1_display_1;
        break;
    case 160:
        capture_display_1.ab[19] = bits1_display_1;
        break;
    case 168:
        capture_display_1.ab[20] = bits1_display_1;
        break;
    case 176:
        capture_display_1.ab[21] = bits1_display_1;
        break;
    case 184:
        capture_display_1.ab[22] = bits1_display_1;
        break;
    case 192:
        capture_display_1.ab[23] = bits1_display_1;
        break;

    default:
        break;
    }

    bit_idx_display_1++;
}

static void IRAM_ATTR data_read_end_dis_1(void *arg)
{
    pin_interrupt_disable_dis_1();
    xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
}

static void IRAM_ATTR bit_read_dis_2(void *arg) // ICACHE_RAM_ATTR  //IRAM_ATTR
{
    bits1_display_2 = (uint32_t)((uint32_t)(c_pin_sdata1_read_dis_1) | (uint32_t)(bits1_display_2 << 1));
    switch (bit_idx_display_2)
    {
        // case 0:
        //     capture_display_2.ab[0] = bits1_display_2;
        //     break;
        // case 1:
        //     capture_display_2.ab[1] = bits1_display_2;
        //     break;
        // case 2:
        //     capture_display_2.ab[2] = bits1_display_2;
        //     break;
        // case 3:
        //     capture_display_2.ab[3] = bits1_display_2;
        //     break;
        // case 4:
        //     capture_display_2.ab[4] = bits1_display_2;
        //     break;
        // case 5:
        //     capture_display_2.ab[5] = bits1_display_2;
        //     break;
        // case 6:
        //     capture_display_2.ab[6] = bits1_display_2;
        //     break;
        // case 7:
        //     capture_display_2.ab[7] = bits1_display_2;
        //     break;

    case 8:
        capture_display_2.ab[0] = bits1_display_2;
        break;
    case 16:
        capture_display_2.ab[1] = bits1_display_2;
        break;
    case 24:
        capture_display_2.ab[2] = bits1_display_2;
        break;
    case 32:
        capture_display_2.ab[3] = bits1_display_2;
        break;
    case 40:
        capture_display_2.ab[4] = bits1_display_2;
        break;
    case 48:
        capture_display_2.ab[5] = bits1_display_2;
        break;
    case 56:
        capture_display_2.ab[6] = bits1_display_2;
        break;
    case 64:
        capture_display_2.ab[7] = bits1_display_2;
        break;
    case 72:
        capture_display_2.ab[8] = bits1_display_2;
        break;
    case 80:
        capture_display_2.ab[9] = bits1_display_2;
        break;
    case 88:
        capture_display_2.ab[10] = bits1_display_2;
        break;
    case 96:
        capture_display_2.ab[11] = bits1_display_2;
        break;
    case 104:
        capture_display_2.ab[12] = bits1_display_2;
        break;
    case 112:
        capture_display_2.ab[13] = bits1_display_2;
        break;
    case 120:
        capture_display_2.ab[14] = bits1_display_2;
        break;
    case 128:
        capture_display_2.ab[15] = bits1_display_2;
        break;
    case 136:
        capture_display_2.ab[16] = bits1_display_2;
        break;
    case 144:
        capture_display_2.ab[17] = bits1_display_2;
        break;
    case 152:
        capture_display_2.ab[18] = bits1_display_2;
        break;
    case 160:
        capture_display_2.ab[19] = bits1_display_2;
        break;
    case 168:
        capture_display_2.ab[20] = bits1_display_2;
        break;
    case 176:
        capture_display_2.ab[21] = bits1_display_2;
        break;
    case 184:
        capture_display_2.ab[22] = bits1_display_2;
        break;
    case 192:
        capture_display_2.ab[23] = bits1_display_2;
        break;

    default:
        break;
    }

    bit_idx_display_2++;
}

static void IRAM_ATTR data_read_end_dis_2(void *arg)
{
    pin_interrupt_disable_dis_2();
    xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
}

/**
 *
 */
static void get_display_data(lgfg_6_digit_t *dd, uint8_t *ab)
{
    lgfg_6_digit_t data = {};

    /* check digit matching */
    for (size_t i = 0; i < DATA_SET_SIZE; i++)
    {
        ab[i] = seg7_to_digit[ab[i]];
    }

    data.unit_price = (uint32_t)(ab[IDX_UNIT_7]) * 10000000 +
                      (uint32_t)(ab[IDX_UNIT_6]) * 1000000 +
                      (uint32_t)(ab[IDX_UNIT_5]) * 100000 +
                      (uint32_t)(ab[IDX_UNIT_4]) * 10000 +
                      (uint32_t)(ab[IDX_UNIT_3]) * 1000 +
                      (uint32_t)(ab[IDX_UNIT_2]) * 100 +
                      (uint32_t)(ab[IDX_UNIT_1]) * 10 +
                      (uint32_t)(ab[IDX_UNIT_0]);

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

    // decimal point of total price and unit price is equal, so consider volume decimal points only
    uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
    gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
    if (gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR)) // if total price should match with unit_price*valume. Check for the tolerance
    {
        data.error.err_bit.price_gap = true;
    }
    memcpy((void *)dd, (void *)&data, sizeof(lgfg_6_digit_t));
}

static void data_send_task(void *arg)
{
    TickType_t ticks_last[TX_ID_DIS_DATA_SIZE] = {xTaskGetTickCount(), xTaskGetTickCount()};
    capture_data_t capture_now = {};
    capture_data_t capture_dis[TX_ID_DIS_DATA_SIZE] = {};
    data_packet_t display_data = {
        .display = DIS_LONGFENG_8_DIGIT,
        .length = sizeof(lgfg_6_digit_t)};
    while (1)
    {
        if (xQueueReceive(capture_queue, &capture_now, pdMS_TO_TICKS(10)))
        {
            const size_t dis_id = capture_now.id;

            // if received data is different from what we have and it is within time range for send
            const TickType_t ticks_now = xTaskGetTickCount();
            if (ticks_now - ticks_last[dis_id] > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) /* || memcmp(capture_dis[dis_id].ab, capture_now.ab, DATA_SET_SIZE)*/)
            {
                const capture_data_t now_temp = capture_now;
                ticks_last[dis_id] = ticks_now;
                display_data.pck_id = capture_now.id;
                get_display_data((lgfg_6_digit_t *)(display_data.ab_data), capture_now.ab);
                if ((((lgfg_6_digit_t *)(display_data.ab_data))->error.u8int & settings.error_mask.u8int) == 0) // if error flasg is non zero, do not send
                {
                    memcpy(capture_dis[dis_id].ab, now_temp.ab, sizeof(now_temp.ab));
                    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));    // if queue is full, wait 10ms until it gets clear
                }
                // else
                //     printf("errors=0x%.2x\r\n", ((lgfg_6_digit_t *)(display_data.ab_data))->error.u8int);

                // printf("dis%d 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x\r\n", now_temp.id,
                //        now_temp.ab[0],
                //        now_temp.ab[1],
                //        now_temp.ab[2],
                //        now_temp.ab[3],
                //        now_temp.ab[4],
                //        now_temp.ab[5],
                //        now_temp.ab[6],
                //        now_temp.ab[7]);

                // const lgfg_6_digit_t *dis = (lgfg_6_digit_t *)(display_data.ab_data);
                // printf("first=0x%.2x%.2x%.2x%.2x\t[16]=0x%.2x\r\n",
                //                     first.tu8int[3].u8int,
                //                     first.tu8int[2].u8int,
                //                     first.tu8int[1].u8int,
                //                     first.tu8int[0].u8int,
                //                     capture_now.ab[0x0F + 0x01].tu8int[0].u8int);
                // printf("dis=%d\tunit=%d\ttotal=%d\tvolume=%d\terr=%d\r\n\r\n", capture_now.id, dis->unit_price, dis->total_price, dis->volume_l, dis->error.u8int);
            }

            while (c_pin_rclk_read_dis_1)
            {
                if (xTaskGetTickCount() > ticks_now + pdMS_TO_TICKS(20))
                    break;
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
            get_display_data((lgfg_6_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        if ((xTaskGetTickCount() - ticks_last[TX_ID_DIS2_DATA]) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
        	capture_data_t temp = capture_dis[TX_ID_DIS2_DATA];
            ticks_last[TX_ID_DIS2_DATA] = xTaskGetTickCount();
            display_data.pck_id = TX_ID_DIS2_DATA;
            get_display_data((lgfg_6_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

static void pin_interrupt_enable_dis_1()
{
    bit_idx_display_1 = 0;
    bits1_display_1 = 0;
    memset(capture_display_1.ab, 0, sizeof(capture_display_1.ab));
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_NEGEDGE);
}
static void pin_interrupt_enable_dis_2()
{
    bit_idx_display_2 = 0;
    bits1_display_2 = 0;
    memset(capture_display_2.ab, 0, sizeof(capture_display_1.ab));
    gpio_set_intr_type(D2_SCLK, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(D2_RCLK, GPIO_INTR_NEGEDGE);
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

esp_err_t display_longfeng_8_digit_init(xQueueHandle *send_que)
{
    gpio_config_t io_conf;
    // set display enable pin output and turn off
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = BIT(DIS_ENB);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set two clock negative edge intterupt pin inputs
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT(D1_SCLK) | BIT(D2_SCLK);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set two clock positive edge intterupt pin inputs
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
    gpio_isr_handler_add(D1_RCLK, data_read_end_dis_1, NULL);
    gpio_isr_handler_add(D2_SCLK, bit_read_dis_2, NULL);
    gpio_isr_handler_add(D2_RCLK, data_read_end_dis_2, NULL);

    pin_interrupt_disable_dis_2();

    capture_queue = xQueueCreate(20, sizeof(capture_data_t));
    if (capture_queue == NULL)
    {
        return ESP_FAIL;
    }

    ptr_send_que = send_que;

    bit_idx_display_1 = 0;
    bits1_display_1 = 0;
    bit_idx_display_2 = 0;
    bits1_display_2 = 0;

    if (xTaskCreate(data_send_task, "data_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        return ESP_FAIL;
    }
    gpio_set_level(DIS_ENB, true);

    printf("Staring Longfeng display\r\n");
    return ESP_OK;
}
