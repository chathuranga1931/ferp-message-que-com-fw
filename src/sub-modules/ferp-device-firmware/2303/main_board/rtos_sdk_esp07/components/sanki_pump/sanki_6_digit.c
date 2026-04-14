#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "settings.h"
#include "sanki_6_digit.h"

// convert price into display counts
#define PRICE_TO_DIS_COUNTS(x) (x * 10) // in Censtar 6 digit, only one decimal point is showing.
#define VOLUME_COUNTS 1000ULL              // in Censtar 6 digit, have three decimal digits

#define DATA_SET_SIZE 18

#define DIS_ENB GPIO_NUM_15

#define D1_SCLK GPIO_NUM_14
#define D1_RCLK GPIO_NUM_12
#define D1_SDATA1 GPIO_NUM_13

#define D2_SCLK GPIO_NUM_4
#define D2_RCLK GPIO_NUM_5
#define D2_SDATA1 GPIO_NUM_2

#define D1_SDATA2 GPIO_NUM_16   //not used
#define D2_SDATA2 GPIO_NUM_0    //not used

#define c_pin_sdata1_read_dis_1 (bool)((GPIO.in & BIT(D1_SDATA1)))

#define c_pin_sdata1_read_dis_2 (bool)((GPIO.in & BIT(D2_SDATA1)))

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
    IDX_UNIT_4 = (0xF + 0x1), //this byte after 16 bytes

    IDX_TOTAL_0 = 4,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_SIZE,

    IDX_VOLUME_0 = 10,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
    IDX_VOLUME_SIZE,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L,

} byte_index_t;

typedef enum
{
    LL_IDX_LL_1 = IDX_TOTAL_4,
    LL_IDX_LL_2 = IDX_TOTAL_5,

    LL_IDX_TOT_LITERS_0 = IDX_VOLUME_0,
    LL_IDX_TOT_LITERS_1,
    LL_IDX_TOT_LITERS_2,
    LL_IDX_TOT_LITERS_3,
    LL_IDX_TOT_LITERS_4,
    LL_IDX_TOT_LITERS_5,

    LL_IDX_TOT_LITERS_6 = IDX_TOTAL_0,
    LL_IDX_TOT_LITERS_7,
    LL_IDX_TOT_LITERS_8,
    LL_IDX_TOT_LITERS_9,
} ll_byte_index_t;

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
    tuByte_t tu8int[4];
    uint32_t u32int;
} tuLong_t;

typedef struct
{
    uint8_t id;
    tuLong_t ab[DATA_SET_SIZE];
} capture_data_t;

static xQueueHandle capture_queue = NULL;
static xQueueHandle *ptr_send_que = NULL;

static bool start_display_1 = false;
static uint32_t bits1_display_1 = 0;
static capture_data_t capture_display_1 = {.id = TX_ID_DIS1_DATA};

static bool start_display_2 = false;
static uint32_t bits1_display_2 = 0;
static capture_data_t capture_display_2 = {.id = TX_ID_DIS2_DATA};

static void pin_interrupt_enable_dis_1();
static void pin_interrupt_enable_dis_2();
static void pin_interrupt_disable_dis_1();
static void pin_interrupt_disable_dis_2();

static void IRAM_ATTR bit_read_dis_1(void *arg)
{
    bits1_display_1 = (uint32_t)((uint32_t)(c_pin_sdata1_read_dis_1) | (uint32_t)(bits1_display_1 << 1));
}

static void IRAM_ATTR bits_read_end_dis_1(void *arg)
{
    const uint8_t index = bits1_display_1 & 0x0F; // get index

    switch (index)
    {
    case 0xF:
        start_display_1 = true;
        break;
    
    default:
        break;
    }

    // switch (start_display_1)
    // {
    // case true:
    //     data1_display_1[index] = bits1_display_1;
    //     bits1_display_1 = 0;
    //     switch (index)
    //     {
    //     case 0:
    //         pin_interrupt_disable_dis_1();
    //         memcpy(capture_display_1.ab, data1_display_1, sizeof(data1_display_1));
    //         xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
    //         break;
        
    //     default:
    //         break;
    //     }
    //     break;
    
    // default:
    //     break;
    // }

    // if (index == 0x0F) // looking for first 0xF index byte and mark start of data capture
    //     start_display_1 = true;

    if (start_display_1)
    {
        // data1_display_1[index] = bits1_display_1;
        capture_display_1.ab[index].u32int = bits1_display_1;
        bits1_display_1 = 0;
        // if end reached
        if (!index)
        {
            pin_interrupt_disable_dis_1();
            // memcpy(capture_display_1.ab, data1_display_1, sizeof(data1_display_1));
            xQueueSendFromISR(capture_queue, (void *)&capture_display_1, NULL);
        }
    }
}

static void IRAM_ATTR bit_read_dis_2(void *arg)
{
    bits1_display_2 = (uint32_t)((uint32_t)(c_pin_sdata1_read_dis_2) | (uint32_t)(bits1_display_2 << 1));
}

static void IRAM_ATTR bits_read_end_dis_2(void *arg)
{
    const uint8_t index = bits1_display_2 & 0x0F; // get index

    switch (index)
    {
    case 0xF:
        start_display_2 = true;
        break;
    
    default:
        break;
    }

    if (start_display_2)
    {
        // data1_display_2[index] = bits1_display_2;
        capture_display_2.ab[index].u32int = bits1_display_2;
        bits1_display_2 = 0;
        // if end reached
        if (!index)
        {
            pin_interrupt_disable_dis_2();
            // memcpy(capture_display_2.ab, data1_display_2, sizeof(data1_display_2));
            xQueueSendFromISR(capture_queue, (void *)&capture_display_2, NULL);
        }
    }
}
/**
 * Sanki 6 digit display
 *  Price has 2 decimal points with rest 3 digits. 3rd gigit is in [1] byte of the packet.
 *  Liters has 3 decimal points with rest 3 digits.
 *  Total price has 2 decimal points with rest 4 digits. After 9999.99 total value 5th digit is missing. Counting only display last 6 digits.
 * Data packet has 18 bytes. Data is on DATA1 pin only. RCLK pulse starts at [2] byte.
 * 0xF0, 0x20, 0xFF, 0xFE, 0x1D, 0x0C, 0x0B, 0x0A, 0xF9, 0x28, 0x87, 0x66, 0x05, 0x04, 0x83, 0x62, 0x01, 0x00
 * None  U[4]  V[5]  V[4]  V[3]  V[2]  V[1]  V[0]  T[5]  T[4]  T[3]  T[2]  T[1]  T[0]  U[3]  U[2]  U[1]  U[0]   
 */

static void get_display_data(sanki_6_digit_t *dd, tuLong_t *ab)
{
    sanki_6_digit_t data = {};

    data.flags.select_ll = (bool)(ab[LL_IDX_LL_1].tu8int[0].MS == DIS_CHARACTOR_L && ab[LL_IDX_LL_2].tu8int[0].MS == DIS_CHARACTOR_L);
    data.flags.select_l = (bool)(ab[IDX_SELECT_L].tu8int[0].MS == DIS_CHARACTOR_L);
    data.flags.select_p = (bool)(ab[IDX_SELECT_P].tu8int[0].MS == DIS_CHARACTOR_P);


    for (int i = 0; i < DATA_SET_SIZE; i++)
    {
        switch (i)
        {
        case IDX_UNIT_0:
        case IDX_UNIT_1:
        case IDX_UNIT_2:
        case IDX_UNIT_3:
            if (ab[i].tu8int[0].LS != i)
            {
                data.error.err_bit.unitprice = true;
                data.error.err_bit.index = true;
            }
            break;
        case IDX_UNIT_4: //located in [16]
            if (ab[i].tu8int[0].LS != 0x0)
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
            if (ab[i].tu8int[0].LS != i)
            {
                data.error.err_bit.totprice = true;
                data.error.err_bit.index = true;
            }
            break;
        case IDX_VOLUME_0:
        case IDX_VOLUME_1:
        case IDX_VOLUME_2:
        case IDX_VOLUME_3:
        case IDX_VOLUME_4:
        case IDX_VOLUME_5:
            if (ab[i].tu8int[0].LS != i)
            {
                data.error.err_bit.volume = true;
                data.error.err_bit.index = true;
            }
            break;
        default:
            break;
        }

        if (ab[i].tu8int[0].MS >= 10)
            ab[i].tu8int[0].MS = 0;
    }

    data.unit_price = (uint32_t)(ab[IDX_UNIT_4].tu8int[0].MS) * 10000UL +
                      (uint32_t)(ab[IDX_UNIT_3].tu8int[0].MS) * 1000UL +
                      (uint32_t)(ab[IDX_UNIT_2].tu8int[0].MS) * 100UL +
                      (uint32_t)(ab[IDX_UNIT_1].tu8int[0].MS) * 10UL +
                      (uint32_t)(ab[IDX_UNIT_0].tu8int[0].MS);

    /*if Total Liter option is selected, calculate it*/
    if (data.flags.select_ll)
    {
        data.total_liters = (uint64_t)(ab[LL_IDX_TOT_LITERS_9].tu8int[0].MS) * 1000000000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_8].tu8int[0].MS) * 100000000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_7].tu8int[0].MS) * 10000000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_6].tu8int[0].MS) * 1000000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_5].tu8int[0].MS) * 100000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_4].tu8int[0].MS) * 10000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_3].tu8int[0].MS) * 1000ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_2].tu8int[0].MS) * 100ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_1].tu8int[0].MS) * 10ULL +
                            (uint64_t)(ab[LL_IDX_TOT_LITERS_0].tu8int[0].MS);
    }
    else
    {
        data.total_price = (uint32_t)(ab[IDX_TOTAL_5].tu8int[0].MS) * 100000UL +
                           (uint32_t)(ab[IDX_TOTAL_4].tu8int[0].MS) * 10000UL +
                           (uint32_t)(ab[IDX_TOTAL_3].tu8int[0].MS) * 1000UL +
                           (uint32_t)(ab[IDX_TOTAL_2].tu8int[0].MS) * 100UL +
                           (uint32_t)(ab[IDX_TOTAL_1].tu8int[0].MS) * 10UL +
                           (uint32_t)(ab[IDX_TOTAL_0].tu8int[0].MS);

        data.volume_l = (uint32_t)(ab[IDX_VOLUME_5].tu8int[0].MS) * 100000UL +
                        (uint32_t)(ab[IDX_VOLUME_4].tu8int[0].MS) * 10000UL +
                        (uint32_t)(ab[IDX_VOLUME_3].tu8int[0].MS) * 1000UL +
                        (uint32_t)(ab[IDX_VOLUME_2].tu8int[0].MS) * 100UL +
                        (uint32_t)(ab[IDX_VOLUME_1].tu8int[0].MS) * 10UL +
                        (uint32_t)(ab[IDX_VOLUME_0].tu8int[0].MS);

        // decimal point of total price and unit price is equal, so consider volume decimal points only
        uint64_t gap = (((uint64_t)data.unit_price * (uint64_t)data.volume_l) / VOLUME_COUNTS);
        gap = gap > (uint64_t)data.total_price ? gap - (uint64_t)data.total_price : (uint64_t)data.total_price - gap;
        if (gap > PRICE_TO_DIS_COUNTS(PRICE_GAP_LKR)) // if total price should match with unit_price*valume. Check for the tolerance
        {
            data.error.err_bit.price_gap = true;
        }
    }

    memcpy((void *)dd, (void *)&data, sizeof(sanki_6_digit_t));
}

static void data_send_task(void *arg)
{
    TickType_t ticks_last[TX_ID_DIS_DATA_SIZE] = { xTaskGetTickCount(), xTaskGetTickCount() };
    capture_data_t capture_now = {};
    capture_data_t capture_dis[TX_ID_DIS_DATA_SIZE] = {};
    data_packet_t display_data = {
        .display = DIS_SANKI_6_DIGIT,
        .length = sizeof(sanki_6_digit_t)};
    while (1)
    {
        if (xQueueReceive(capture_queue, &capture_now, pdMS_TO_TICKS(10)))
        {
            const size_t dis_id = capture_now.id;
            const tuLong_t first = capture_now.ab[0xF];

            //check for validity
            if(first.tu8int[0].LS != 0xF || first.tu8int[1].LS != 0 || first.tu8int[1].MS == 0 || first.tu8int[2].LS != 0)
            {
                // printf("0x%.2x%.2x%.2x%.2x\r\n\r\n", first.tu8int[3].u8int, 
                //                                 first.tu8int[2].u8int, 
                //                                 first.tu8int[1].u8int, 
                //                                 first.tu8int[0].u8int);
                goto end; //drop packet
            }
                

            capture_now.ab[0x0F + 0x01].tu8int[0].u8int = first.tu8int[1].u8int; // put it into [16] byte
            capture_now.ab[0x0F + 0x02].tu8int[0].u8int = first.tu8int[2].u8int; // put it into [17] byte

            // if received data is different from what we have and it is within time range for send
            const TickType_t ticks_now = xTaskGetTickCount();
            if (ticks_now - ticks_last[dis_id] > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS))// && memcmp(capture_dis[dis_id].ab, capture_now.ab, DATA_SET_SIZE))
            {
				const capture_data_t now_temp = capture_now;
                ticks_last[dis_id] = ticks_now;
                display_data.pck_id = capture_now.id;
                get_display_data((sanki_6_digit_t *)(display_data.ab_data), capture_now.ab);
                if ((((sanki_6_digit_t *)(display_data.ab_data))->error.u8int & settings.error_mask.u8int) == 0) // if error flasg is non zero, do not send
                {
                    memcpy(capture_dis[dis_id].ab, now_temp.ab, sizeof(now_temp.ab));
                    xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));    // if queue is full, wait 10ms until it gets clear
                }
                    // else
                //     printf("errors=0x%.2x\r\n", ((sanki_6_digit_t *)(display_data.ab_data))->error.u8int);

                // const sanki_6_digit_t *dis = (sanki_6_digit_t *)(display_data.ab_data);
                // printf("first=0x%.2x%.2x%.2x%.2x\t[16]=0x%.2x\r\n",
                //                     first.tu8int[3].u8int, 
                //                     first.tu8int[2].u8int, 
                //                     first.tu8int[1].u8int, 
                //                     first.tu8int[0].u8int,
                //                     capture_now.ab[0x0F + 0x01].tu8int[0].u8int);
                // printf("dis=%d\tunit=%d\ttotal=%d\tvolume=%d\terr=%d\r\n\r\n", capture_now.id, dis->unit_price, dis->total_price, dis->volume_l, dis->error.u8int);
            }
        end:
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
            get_display_data((sanki_6_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        if ((xTaskGetTickCount() - ticks_last[TX_ID_DIS2_DATA]) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
			capture_data_t temp = capture_dis[TX_ID_DIS2_DATA];
            ticks_last[TX_ID_DIS2_DATA] = xTaskGetTickCount();
            display_data.pck_id = TX_ID_DIS2_DATA;
            get_display_data((sanki_6_digit_t *)(display_data.ab_data), temp.ab);
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

static void pin_interrupt_enable_dis_1()
{
    start_display_1 = false;
    // memset(data1_display_1, 0, sizeof(data1_display_1));
    memset(capture_display_1.ab, 0, sizeof(capture_display_1.ab));
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_enable_dis_2()
{
    start_display_2 = false;
    // memset(data1_display_2, 0, sizeof(data1_display_2));
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

esp_err_t display_sanki_6_digit_init(xQueueHandle *send_queue)
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

    ptr_send_que = send_queue;

    bits1_display_1 = 0;
    bits1_display_2 = 0;

    if (xTaskCreate(data_send_task, "data_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        return ESP_FAIL;
    }
    gpio_set_level(DIS_ENB, true);

    // printf("Staring Censtar 6 pump\r\n");
    return ESP_OK;
}