#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/uart.h>
#include "board.h"
#include "com_distap.h"

#define UART0_BUFF 256 + 1

typedef struct
{
    uint32_t time_ms;
} tx_id_alive_t;

static uint8_t rx_buffer[UART0_BUFF];
static uint8_t tx_buffer[UART0_BUFF];
static void (*dis1_cb)(display_type_t type, uint8_t *data);
static void (*dis2_cb)(display_type_t type, uint8_t *data);
static data_packet_t *rx_packet = NULL;
static bool got_data = false;
static TaskHandle_t ser_rx_task_hdl = NULL;
static bool suspend_task = false;

// void read_response(data_packet_t *packet);
static uint16_t dev_crc16(uint16_t wcrc, const void *kpvData, uint32_t length);
static size_t make_tx_buffer(data_packet_t *packet, uint8_t *buffer);
static bool validate_packet(data_packet_t *packet, int *idx);

display_type_t display_type;

bool distap_send_cmd(data_packet_t *rx, data_packet_t *tx, uint32_t tout)
{
    const size_t len = tx->length + offsetof(data_packet_t, ab_data);
    const uint16_t wcrc = dev_crc16(0xFFFF, tx->ab_raw, len);
    tx->ab_raw[len] = (uint8_t)wcrc;
    tx->ab_raw[len + 1] = (uint8_t)(wcrc >> 8);
    const size_t size = make_tx_buffer(tx, tx_buffer);
    // send packet
    rx_packet = rx;
    got_data = false;
    uart_wait_tx_done(UART_NUM_2, portMAX_DELAY); // wait until transmit finishes
    uart_write_bytes(UART_NUM_2, (char *)tx_buffer, size);
    uart_wait_tx_done(UART_NUM_2, portMAX_DELAY); // wait until transmit finishes
    const TickType_t tick_pre = xTaskGetTickCount();
    TickType_t tick_send = tick_pre;
    while (!got_data)
    {
        if (xTaskGetTickCount() - tick_send > pdMS_TO_TICKS(1 * 1000)) //polling again if wait more than 1 second
        {
            uart_wait_tx_done(UART_NUM_2, portMAX_DELAY); // wait until transmit finishes
            uart_write_bytes(UART_NUM_2, (char *)tx_buffer, size);
            uart_wait_tx_done(UART_NUM_2, portMAX_DELAY); // wait until transmit finishes
            tick_send = xTaskGetTickCount();
        }
        /*timeout for the reply*/
        if (xTaskGetTickCount() - tick_pre > pdMS_TO_TICKS(tout))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static void read_response(data_packet_t *packet)
{
    switch ((rx_pckt_id_t)packet->pck_id)
    {
    case RX_ID_DIS1_DATA:
        if (dis1_cb)
            dis1_cb(packet->display, packet->ab_data);
        break;
    case RX_ID_DIS2_DATA:
        if (dis2_cb)
            dis2_cb(packet->display, packet->ab_data);
        break;
    case RX_ID_KEEP_ALIVE:
        /* keep alive packet has time in ms from start*/
        // ((tx_id_alive_t*)packet->ab_data)->time_ms;
        // printf("keep alive\r\n");
        break;
    case RX_ID_LOG_PRINTS:
        printf("%.*s", packet->length, (const char*)packet->ab_data);
        break;
    default:
        // printf("packet:%d\r\n", packet->pck_id);
        if (rx_packet)
        {
            memcpy(rx_packet, packet, sizeof(data_packet_t));
            got_data = true;
        }
        break;
    }
}

static void serial_receive_task(void *arg)
{
    int rx_idx = -1;
    bool rx_doesc = false;
    int len, index = 0;
    TickType_t msg_ticks = xTaskGetTickCount();
    data_packet_t packet = {};
    uart_flush(UART_NUM_2);
    bool need_to_send_set_display = false;

    while (1)
    {
        uint8_t *buffer = (uint8_t *)(rx_buffer + index);
        len = uart_read_bytes(UART_NUM_2, buffer, (UART0_BUFF - index), pdMS_TO_TICKS(1));
        if (len)
        {           
            for (size_t i = 0; i < len; i++)
            {
                // printf("len:%d, index:%d - ", len, index);
                // for (size_t i = 0; i < len; i++)
                // {
                //     printf("0x%0.2x, ", buffer[i]);
                // }
                // printf("\r\n");
                
                // are we awaiting the SOM?
                if (rx_idx == -1)
                {
                    // printf("%d\r\n", __LINE__);
                    // yes, is this the SOM?
                    if (buffer[i] == 0xFF)
                    {
                        rx_idx = 0; // yes, so flag we're now storing bytes
                    }
                    continue;
                }
                else if (buffer[i] == 0xFF) // EOM received?
                {
                    rx_doesc = false;

                    // yes, have we received enough data?
                    if (rx_idx >= 4)
                    {
                        // yes, we may have whole message, signal we're no longer receiving
                        const bool valid = validate_packet(&packet, &rx_idx);
                        if (valid)
                        {
                            msg_ticks = xTaskGetTickCount();
                            read_response(&packet);
                        }
                        else
                        {
                            printf("invalid\r\n");
                        }
                        
                        uart_flush(UART_NUM_2);
                        rx_idx = -1;
                        index = 0;
                        break; // going out from for loop
                    }
                    else
                    {
                        // otherwise not enough data, so dump whatever is there
                        rx_idx = 0;
                    }
                    continue;
                }

                if (buffer[i] == 0xFE) // escaped char marker
                {
                    // yes, so flag the next char must be unescaped
                    rx_doesc = true;
                    continue;
                }

                if (rx_doesc) // need to unescape?
                {
                    // yes, so do it
                    buffer[i] = ~(buffer[i]);
                    rx_doesc = false; // clear the flag
                }

                // index within range?
                if (rx_idx < sizeof(data_packet_t))
                {
                    // yes, so save the data
                    packet.ab_raw[rx_idx++] = buffer[i];
                }
                else
                {
                    // flag too many bytes received, revert to awaiting SOM
                    rx_idx = -1;
                }
            }
        }
        // Updating index
        index += len;
        if (index >= UART0_BUFF)
            index = 0;

        // If no data was received, yield to allow the idle task to run.
        // uart_read_bytes() with pdMS_TO_TICKS(1) rounds to 0 ticks at
        // CONFIG_FREERTOS_HZ=100 (10 ms/tick), so it returns immediately
        // without blocking — causing a busy-loop that starves the idle task
        // and triggers the Task Watchdog.
        if (len == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (suspend_task)
        {
            vTaskSuspend(NULL);
            while (suspend_task) //wait until suspend task
            {
                vTaskDelay(1);
            }
            msg_ticks = xTaskGetTickCount(); //restart timeout
        }
        /* check not alive for minute*/
        if(xTaskGetTickCount() - msg_ticks > pdMS_TO_TICKS(30*1000))
        {
            printf("No reply, resetting distap\r\n");
            gpio_set_reset_distap(true);
            vTaskDelay(pdMS_TO_TICKS(20));
            gpio_set_reset_distap(false);
            msg_ticks = xTaskGetTickCount();
            need_to_send_set_display = true;
        }

        // if(need_to_send_set_display && (xTaskGetTickCount() - msg_ticks > pdMS_TO_TICKS(30*1000))){
        //     vTaskDelay(pdMS_TO_TICKS(100));
        //     msg_ticks = xTaskGetTickCount();
        //     int status = distap_set_display_type(display_type);
        //     if(status == ESP_OK){
        //         need_to_send_set_display = false;
        //     }
        // }
    }
    vTaskDelete(NULL);
}

esp_err_t init_comms_distap(void (*dis1_fuel_event)(display_type_t type, uint8_t *data), void (*dis2_fuel_event)(display_type_t type, uint8_t *data))
{
    esp_err_t ret = ESP_OK;
    dis1_cb = dis1_fuel_event;
    dis2_cb = dis2_fuel_event;
    if (ser_rx_task_hdl != NULL)
        return ret;
    if (xTaskCreate(serial_receive_task, "serial_receive_task", 6 * 1024, NULL, 5, &ser_rx_task_hdl) != pdPASS)
    {
        return ESP_FAIL;
    }
    return ret;
}

void suspend_comms_distap()
{
    TaskStatus_t pxTaskStatus;
    BaseType_t xGetFreeStackSpace;
    eTaskState eState;

    suspend_task = true;
    /*wait until task suspend*/
    printf("suspending distap comm task\r\n");
    vTaskDelay(pdMS_TO_TICKS(1 * 1000));
}

void resume_comms_distap()
{
    // reset back original baudrate
    if(uart_is_driver_installed(UART_NUM_2))
        uart_set_baudrate(UART_NUM_2, UART2_BAUDRATE);
    suspend_task = false;
    if (ser_rx_task_hdl)
        vTaskResume(ser_rx_task_hdl);
    /*wait until task resume*/
    printf("resuming distap comm task\r\n");
}

static size_t make_tx_buffer(data_packet_t *packet, uint8_t *buffer)
{
    size_t idx = 0;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;                             // adding 2 0XFF s to the begning of the packet
    for (size_t i = 0; i < (packet->length + offsetof(data_packet_t, ab_data) + 2); i++) // pack_id + address + length + crcH + crcL
    {
        if (packet->ab_raw[i] == 0xFF)
        {
            buffer[idx++] = 0XFE;
            buffer[idx++] = 0x00;
        }
        else if (packet->ab_raw[i] == 0xFE)
        {
            buffer[idx++] = 0XFE;
            buffer[idx++] = 0x01;
        }
        else
        {
            buffer[idx++] = packet->ab_raw[i];
        }
    }
    buffer[idx++] = 0xFF; // mark end
    return idx;
}
static bool validate_packet(data_packet_t *packet, int *idx)
{
    // was there no response?
    if (*idx <= 0)
    {
        return false;
    }
    // expecting as valid id
    if (!(packet->pck_id < RX_ID_SIZE && packet->display < DIS_SIZE))
    {
        return false;
    }
    // calc total length
    const size_t b = packet->length + offsetof(data_packet_t, ab_data) + 2; //pack_id + address + lenght + CRCLSB + CRCMSB
    if (b != *idx)
    {
        return false;
    }
    const uint16_t wcrc = dev_crc16(0xFFFF, packet->ab_raw, b);
    if (wcrc)
    {
        return false;
    }

    return true;
}
static uint16_t dev_crc16(uint16_t wcrc, const void *kpvData, uint32_t length)
{
    /* Table of CRC values for high�order byte */
    static const uint8_t kabCRCHi[256] =
        {
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
            0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40};
    /* Table of CRC values for low�order byte */
    static const uint8_t kabCRCLo[] =
        {
            0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04,
            0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 0x08, 0xC8,
            0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC,
            0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3, 0x11, 0xD1, 0xD0, 0x10,
            0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
            0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38,
            0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C,
            0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26, 0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0,
            0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4,
            0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
            0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C,
            0xB4, 0x74, 0x75, 0xB5, 0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0,
            0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54,
            0x9C, 0x5C, 0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98,
            0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
            0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80, 0x40};
    const uint8_t *kpbData = kpvData;
    uint8_t b;
    uint8_t bCRCLo = (uint8_t)wcrc;
    uint8_t bCRCHi = wcrc >> 8;

    // loop through the buffer
    while (1)
    {
        // get the data byte and move the pointer to the next
        b = *(kpbData++);

        // no length specified AND has the NULL terminator been reached?
        if (!length && !b)
            // yes, so end
            break;

        // calculate the CRC
        b = bCRCLo ^ b;
        bCRCLo = bCRCHi ^ kabCRCHi[b];
        bCRCHi = kabCRCLo[b];

        // has a length been specified?
        if (length)
        {
            // yes, so count down
            length--;

            // reached the end?
            if (!length)
                // yes, so end
                break;
        }
    }
    return ((bCRCHi << 8) | bCRCLo);
}