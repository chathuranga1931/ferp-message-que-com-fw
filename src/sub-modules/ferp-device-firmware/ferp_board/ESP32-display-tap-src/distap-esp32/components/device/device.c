#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "board_io.h"
#include "device.h"
#include "sys_types.h"
#include "settings.h"
#include "production_io.h"
#include "raw_capture.h"
#if !CONFIG_DISTAP_RAW_CAPTURE_ONLY
#include "censtar_6_digit.h"
#include "censtar_7_digit.h"
#include "censtar_7cs_digit.h"
#include "hongyang_8_digit.h"
#include "longfeng_8_digit.h"
#include "sanki_6_digit.h"
#include "wayn_6_digit.h"
#endif // !CONFIG_DISTAP_RAW_CAPTURE_ONLY

#define UART0_BUFF 256 + 1

static QueueHandle_t send_queue = NULL;
uint8_t rx_buffer[UART0_BUFF];
uint8_t tx_buffer[UART0_BUFF];

static void send_response(data_packet_t *packet);
static uint16_t dev_crc16(uint16_t wcrc, const void *kpvData, uint32_t length);
static void serial_send_task(void *arg);
static void serial_receive_task(void *arg);
static bool validate_packet(data_packet_t *packet, int *idx);
static size_t make_tx_buffer(data_packet_t *packet, uint8_t *buffer);

esp_err_t device_init()
{
    esp_err_t ret = ESP_OK;

    if (uart_is_driver_installed(UART_NUM_0))
        uart_driver_delete(UART_NUM_0);
    const uart_config_t uart_config_dev = {
        .baud_rate = UART0_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_GOTO(ret, end, uart_param_config(UART_NUM_0, &uart_config_dev));
    ESP_ERROR_GOTO(ret, end, uart_set_pin(UART_NUM_0, UART0_TX, UART0_RX, GPIO_NUM_NC, GPIO_NUM_NC));
    ESP_ERROR_GOTO(ret, end, uart_driver_install(UART_NUM_0, UART0_BUFF, UART0_BUFF, 0, NULL, 0));

    send_queue = xQueueCreate(20, sizeof(data_packet_t));
    if (send_queue == NULL)
    {
        ret = ESP_FAIL;
        goto end;
    }
    
    if (xTaskCreate(serial_send_task, "serial_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        ret = ESP_FAIL;
        goto end;
    }
    if (xTaskCreate(serial_receive_task, "serial_receive_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        ret = ESP_FAIL;
        goto end;
    }

    switch (settings.display)
    {
    case DIS_NONE: // during production
        ret = production_io_init();
        break;
#if !CONFIG_DISTAP_RAW_CAPTURE_ONLY
    case DIS_CENSTAR_6_DIGIT:
        ret = display_censtar_6_digit_init(&send_queue);
        break;
    case DIS_CENSTAR_7_DIGIT:
        ret = display_censtar_7_digit_init(&send_queue);
        break;
    case DIS_CENSTAR_7CS_DIGIT:
        ret = display_censtar_7cs_digit_init(&send_queue);
        break;
    case DIS_HONGYANG_8_DIGIT:
        ret = display_hongyang_8_digit_init(&send_queue);
        break;
    case DIS_WAYNE_6_DIGIT:
        ret = display_wayne_6_digit_init(&send_queue);
        break;
    case DIS_SANKI_6_DIGIT:
        ret = display_sanki_6_digit_init(&send_queue);
        break;
    case DIS_LONGFENG_8_DIGIT:
        ret = display_longfeng_8_digit_init(&send_queue);
        break;
#endif // !CONFIG_DISTAP_RAW_CAPTURE_ONLY
    case DIS_RAW_8BIT_V1:
        ret = display_raw_capture_init(&send_queue, 8);
        break;
    case DIS_RAW_12BIT_V1:
        ret = display_raw_capture_init(&send_queue, 12);
        break;
    default:
        break;
    }

end:
    return ret;
}

void device_print_log(const char *str, size_t len)
{
    data_packet_t log_data = {
        .pck_id = TX_ID_LOG_PRINTS,
        .length = len
    };
    memcpy(log_data.ab_data, str, len);
    if(send_queue)
        xQueueSend(send_queue, (void *)&log_data, pdMS_TO_TICKS(10));
}

static size_t make_tx_buffer(data_packet_t *packet, uint8_t *buffer)
{
    size_t idx = 0;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;                             // adding 2 0XFF s to the begning of the packet
    for (size_t i = 0; i < (packet->length + offsetof(data_packet_t, ab_data) + 2); i++) // add pack_id + address + length + crcH + crcL
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
    const size_t b = packet->length + offsetof(data_packet_t, ab_data) + 2; //packet id + address + data lenght + CRCLSB + CRCMSB
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

static void serial_receive_task(void *arg)
{
    int rx_idx = -1;
    bool rx_doesc = false;
    int len, index = 0;
    data_packet_t packet = {};
    uart_flush(UART_NUM_0);
    while (1)
    {
        uint8_t *buffer = (uint8_t *)(rx_buffer + index);
        len = uart_read_bytes(UART_NUM_0, buffer, (UART0_BUFF - index), pdMS_TO_TICKS(10));
        if (len)
        {
            // ESP_LOGI(__func__, "len:%d\r\n", len);
            for (size_t i = 0; i < len; i++)
            {
                // int str_len = 0;
                // send_str[0] = '\0';
                // for (size_t i = 0; i < len; i++)
                // {
                //     str_len += sprintf(send_str + str_len, "0x%.2x, ", buffer[i]);
                // }
                // str_len += sprintf(send_str + str_len, "\r\n");
                
                // LOG_PRINT("len:%d i:%d - %s\r\n", len, index, send_str);
                // are we awaiting the SOM?
                if (rx_idx == -1)
                {
                    // LOG_PRINT("line - %d\r\n", __LINE__);
                    // yes, is this the SOM?
                    if (buffer[i] == 0xFF)
                    {
                        rx_idx = 0; // yes, so flag we're now storing bytes
                        // LOG_PRINT("line - %d\r\n", __LINE__);
                    }
                    continue;
                }
                else if (buffer[i] == 0xFF) // EOM received?
                {
                    rx_doesc = false;
                    // LOG_PRINT("line - %d,%d\r\n", __LINE__, rx_idx);
                    // yes, have we received enough data?
                    if (rx_idx >= 4)
                    {
                        // yes, we may have whole message, signal we're no longer receiving
                        const bool valid = validate_packet(&packet, &rx_idx);
                        if (valid)
                        {
                            send_response(&packet);
                        }
                        // else 
                        // {
                        //     LOG_PRINT("invalid \r\n");
                        // }
                        uart_flush(UART_NUM_0);
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
            
            // LOG_PRINT("  \r\n");
        }
        
        // Updating index
        index += len;
        if (index >= UART0_BUFF)
            index = 0;
    }
    vTaskDelete(NULL);
}

// #define DEBUG_PACKET //enable for debug display

static void serial_send_task(void *arg)
{
    data_packet_t display_data = {};
    while (1)
    {
        if (xQueueReceive(send_queue, &display_data, pdMS_TO_TICKS(10000))) // wait 10 seconds and send previous message
        {
        #ifdef DEBUG_PACKET
            // const hya_8_digit_t *data = (hya_8_digit_t *)display_data.ab_data;
            // const cens_6_digit_t *data = (cens_6_digit_t *)display_data.ab_data;
            // const cens_7_digit_t *data = (cens_7_digit_t *)display_data.ab_data;
            const sanki_6_digit_t *data = (sanki_6_digit_t *)display_data.ab_data;
            // const lgfg_6_digit_t *data = (lgfg_6_digit_t *)display_data.ab_data;
            if(data->flags.select_ll)
                printf("dis=%d\tunit=%u\ttotalizer=%lld\terr=0x%.2x\r\n", display_data.pck_id, data->unit_price, (uint64_t)data->total_liters, data->error.u8int);
            else
                printf("dis=%d\tunit=%u\ttotal=%u\tvolume=%u\terr=0x%.2x\r\n", display_data.pck_id, data->unit_price, data->total_price, data->volume_l, data->error.u8int);
        #else
            const size_t len = display_data.length + offsetof(data_packet_t, ab_data); // size upto ab_data[]
            const uint16_t wcrc = dev_crc16(0xFFFF, display_data.ab_raw, len);
            display_data.ab_raw[len] = (uint8_t)wcrc;
            display_data.ab_raw[len + 1] = (uint8_t)(wcrc >> 8);
            const size_t size = make_tx_buffer(&display_data, tx_buffer);
            // printf("\r\n");
            // for (size_t i = 0; i < len+2; i++)
            // {
            //     printf("0x%.2x, ", display_data.ab_raw[i]);
            // }
            // printf("\r\n");
            // for (size_t i = 0; i < size; i++)
            // {
            //     printf("0x%.2x, ", tx_buffer[i]);
            // }
            // printf("\r\n\r\n");
            // send packet
            uart_wait_tx_done(UART_NUM_0, portMAX_DELAY); // wait until transmit finishes
            uart_write_bytes(UART_NUM_0, (char *)tx_buffer, size);
            uart_wait_tx_done(UART_NUM_0, portMAX_DELAY); // wait until transmit finishes
        #endif
            vTaskDelay(pdMS_TO_TICKS(10));                // wait 10ms to add packet gap
        }
        else
        {
        #ifndef DEBUG_PACKET
            // if not packets for more than 10 seconds, send keep alive packet
            display_data.display = (uint8_t)DIS_NONE;
            display_data.pck_id = TX_ID_KEEP_ALIVE;
            display_data.length = sizeof(tx_id_alive_t);
            ((tx_id_alive_t*)(display_data.ab_data))->time_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            
            const size_t len = display_data.length + offsetof(data_packet_t, ab_data); // size upto ab_data[]
            const uint16_t wcrc = dev_crc16(0xFFFF, display_data.ab_raw, len);
            display_data.ab_raw[len] = (uint8_t)wcrc;
            display_data.ab_raw[len + 1] = (uint8_t)(wcrc >> 8);
            const size_t size = make_tx_buffer(&display_data, tx_buffer);
            // send packet
            uart_wait_tx_done(UART_NUM_0, portMAX_DELAY); // wait until transmit finishes
            uart_write_bytes(UART_NUM_0, (char *)tx_buffer, size);
            uart_wait_tx_done(UART_NUM_0, portMAX_DELAY); // wait until transmit finishes
        #endif
            vTaskDelay(pdMS_TO_TICKS(10));                // wait 10ms to add packet gap
        }
    }
    vTaskDelete(NULL);
}

static void send_response(data_packet_t *packet)
{
    data_packet_t tx_packet = (data_packet_t){
        .display = (uint8_t)settings.display};
    
    // ESP_LOGI(__func__, "packet id: %d", packet->pck_id);

    switch ((rx_pckt_id_t)packet->pck_id)
    {
    case RX_ID_DEV_INFO:
    {
        tx_packet.pck_id = TX_ID_DEV_INFO;
        switch ((id_dev_cmd_t)((rx_id_dev_info_t *)packet->ab_data)->command)
        {
        case ID_CMD_DEV_VERSION:
        {
            tx_packet.length = sizeof(tx_id_dev_version_t);
            const tx_id_dev_version_t ver = {
                .command = ID_CMD_DEV_VERSION,
                .version = PROJECT_VER,
            };
            memcpy(tx_packet.ab_data, &ver, sizeof(tx_id_dev_version_t));
        }
        break;
        case ID_CMD_DEV_PROJ_NAME:
        {
            tx_packet.length = sizeof(tx_id_dev_prj_name_t);
            const tx_id_dev_prj_name_t name = {
                .command = ID_CMD_DEV_PROJ_NAME,
                .prj_name = PROJECT_NAME,
            };
            memcpy(tx_packet.ab_data, &name, sizeof(tx_id_dev_prj_name_t));
        }
        break;
        case ID_CMD_DEV_TIMEDATE:
        {
            tx_packet.length = sizeof(tx_id_dev_timedate_t);
            const tx_id_dev_timedate_t time = {
                .command = ID_CMD_DEV_TIMEDATE,
                .time = PROJECT_TIME,
                .date = PROJECT_DATE};
            memcpy(tx_packet.ab_data, &time, sizeof(tx_id_dev_timedate_t));
        }
        break;
        default:
        {
            data_packet_t tx_packet = (data_packet_t){
                .pck_id = TX_ID_NACK,
                .length = sizeof(tx_id_nack_t)};
            const tx_id_nack_t nack = {
                .reason = ID_NACK_INVALID};
            memcpy(tx_packet.ab_data, &nack, sizeof(tx_id_nack_t));
        }
        break;
        }
    }
    break;

    case RX_ID_SET_DISPLAY:
    {
        if (((rx_id_set_display_t *)packet->ab_data)->display < DIS_SIZE ||
            is_raw_capture_type(((rx_id_set_display_t *)packet->ab_data)->display))
        {
            if (settings.display != ((rx_id_set_display_t *)packet->ab_data)->display)
            {
                settings.display = ((rx_id_set_display_t *)packet->ab_data)->display;
                save_system_settings();
                tx_packet.pck_id = TX_ID_SET_DISPLAY;
                tx_packet.display = settings.display;
                tx_packet.length = sizeof(tx_id_set_display_t);
                const tx_id_set_display_t set = {
                    .state = ID_CMD_DIS_CHANGED,
                    .display = settings.display};
                memcpy(tx_packet.ab_data, &set, sizeof(tx_id_set_display_t));
                xQueueSend(send_queue, (void *)&tx_packet, pdMS_TO_TICKS(200)); // if queue is full, wait 10ms until it gets clear
                vTaskDelay(pdMS_TO_TICKS(100));                                // wait 100ms until packet deliver
                esp_restart();                                                 // restart the device;
            }
            else
            {
                tx_packet.pck_id = TX_ID_SET_DISPLAY;
                tx_packet.length = sizeof(tx_id_set_display_t);
                const tx_id_set_display_t set = {
                    .state = ID_CMD_DIS_ALREADY,
                    .display = settings.display};
                memcpy(tx_packet.ab_data, &set, sizeof(tx_id_set_display_t));
            }
        }
        else
        {
            tx_packet.pck_id = TX_ID_NACK;
            tx_packet.length = sizeof(tx_id_nack_t);
            const tx_id_nack_t nack = {
                .reason = ID_NACK_OVERSIZE};
            memcpy(tx_packet.ab_data, &nack, sizeof(tx_id_nack_t));
        }
    }
    break;

    case RX_ID_SET_ERR_MASK:
    {
        // if (((rx_id_set_err_mask_t *)packet->ab_data)->display < DIS_SIZE)
        // {
        if (settings.error_mask.u8int != ((rx_id_set_err_mask_t *)packet->ab_data)->err_mask)
        {
            settings.error_mask.u8int = ((rx_id_set_err_mask_t *)packet->ab_data)->err_mask;
            save_system_settings();
            tx_packet.pck_id = TX_ID_SET_ERR_MASK;
            tx_packet.length = sizeof(tx_id_set_err_mask_t);
            const tx_id_set_err_mask_t set = {
                .state = ID_CMD_ERR_MASK_CHANGED,
                .err_mask = settings.error_mask.u8int
            };
            memcpy(tx_packet.ab_data, &set, sizeof(tx_id_set_err_mask_t));
            xQueueSend(send_queue, (void *)&tx_packet, pdMS_TO_TICKS(200)); // if queue is full, wait 10ms until it gets clear
            vTaskDelay(pdMS_TO_TICKS(100));                                // wait 100ms until packet deliver
            esp_restart();                                                 // restart the device;
        }
        else
        {
            tx_packet.pck_id = TX_ID_SET_ERR_MASK;
            tx_packet.length = sizeof(tx_id_set_err_mask_t);
            const tx_id_set_err_mask_t set = {
                .state = ID_CMD_ERR_MASK_ALREADY,
                .err_mask = settings.error_mask.u8int
            };
            memcpy(tx_packet.ab_data, &set, sizeof(tx_id_set_err_mask_t));
        }
        // }
        // else
        // {
        //     tx_packet.pck_id = TX_ID_NACK;
        //     tx_packet.length = sizeof(tx_id_nack_t);
        //     const tx_id_nack_t nack = {
        //         .reason = ID_NACK_OVERSIZE};
        //     memcpy(tx_packet.ab_data, &nack, sizeof(tx_id_nack_t));
        // }
    }
    break;

    case RX_ID_INPUTS:
    {
        // ESP_LOGI(__func__, "\tinput cmd %d", ((rx_id_inputs_t *)packet->ab_data)->command);
        switch ((id_inputs_cmd_t)(((rx_id_inputs_t *)packet->ab_data)->command))
        {
        case ID_CMD_INPUTS_PINS:
        {
            tx_packet.pck_id = TX_ID_INPUTS;
            tx_packet.length = sizeof(tx_id_inputs_t);
            const tx_id_inputs_t inputs = {
                .inputs = dis_inputs_get()};
            memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
        }
        break;
        case ID_CMD_INPUTS_DIS_ON:
        {
            if (settings.display == DIS_NONE)
            {
                dis_enb_output_set(true);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_DIS_OFF:
        {
            if (settings.display == DIS_NONE)
            {
                dis_enb_output_set(false);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_LED_ON:
        {
            if (settings.display == DIS_NONE)
            {
                dis_led_output_set(true);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_LED_OFF:
        {
            if (settings.display == DIS_NONE)
            {
                dis_led_output_set(false);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_CS1_ON:
        {
            if (settings.display == DIS_NONE)
            {
                dis_out_cs1_set(true);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_CS1_OFF:
        {
            if (settings.display == DIS_NONE)
            {
                dis_out_cs1_set(false);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_CS2_ON:
        {
            if (settings.display == DIS_NONE)
            {
                dis_out_cs2_set(true);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        case ID_CMD_INPUTS_CS2_OFF:
        {
            if (settings.display == DIS_NONE)
            {
                dis_out_cs2_set(false);
                vTaskDelay(pdMS_TO_TICKS(10));
                tx_packet.pck_id = TX_ID_INPUTS;
                tx_packet.length = sizeof(tx_id_inputs_t);
                const tx_id_inputs_t inputs = {
                    .inputs = dis_inputs_get()};
                memcpy(tx_packet.ab_data, &inputs, sizeof(tx_id_inputs_t));
            }
        }
        break;
        default:
        {
            tx_packet.pck_id = TX_ID_NACK;
            tx_packet.length = sizeof(tx_id_nack_t);
            const tx_id_nack_t nack = {
                .reason = ID_NACK_INVALID};
            memcpy(tx_packet.ab_data, &nack, sizeof(tx_id_nack_t));
        }
        break;
        }
    }
    break;

    default:
    {
        tx_packet.pck_id = TX_ID_NACK;
        tx_packet.length = sizeof(tx_id_nack_t);
        const tx_id_nack_t nack = {
            .reason = ID_NACK_INVALID};
        memcpy(tx_packet.ab_data, &nack, sizeof(tx_id_nack_t));
    }
    break;
    }

    xQueueSend(send_queue, (void *)&tx_packet, pdMS_TO_TICKS(200)); // if queue is full, wait 10ms until it gets clear
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
