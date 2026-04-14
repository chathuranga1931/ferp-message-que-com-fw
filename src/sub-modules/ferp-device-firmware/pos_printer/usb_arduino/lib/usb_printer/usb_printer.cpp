
#include <Arduino.h>
// #include <stdlib.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/semphr.h"
// #include "esp_log.h"
#include "usb/usb_host.h"
// #include <string.h>
// #include <stdio.h>
#include "usb_printer.h"

#define DAEMON_TASK_PRIORITY 7
#define CLASS_TASK_PRIORITY 8


#define CLIENT_NUM_EVENT_MSG        5

#define ACTION_OPEN_DEV             0x01
#define ACTION_GET_DEV_INFO         0x02
#define ACTION_GET_DEV_DESC         0x04
#define ACTION_GET_CONFIG_DESC      0x08
#define ACTION_GET_STR_DESC         0x10
#define ACTION_CLOSE_DEV            0x20
#define ACTION_EXIT                 0x40
#define ACTION_SEND_PRING_COMMAND   0x80 //was 0x60 

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
    uint8_t *printer_data;
    size_t print_len;
} class_driver_t;

static SemaphoreHandle_t signaling_sem = NULL;
static TaskHandle_t daemon_task_hdl;
static TaskHandle_t class_driver_task_hdl;
static class_driver_t driver_obj = {0};

static const char *TAG = "CLASS";

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (driver_obj->dev_addr == 0) {
            driver_obj->dev_addr = event_msg->new_dev.address;
            //Open the device next
            driver_obj->actions |= ACTION_OPEN_DEV;
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (driver_obj->dev_hdl != NULL) {
            //Cancel any other actions and close the device next
            driver_obj->actions = ACTION_CLOSE_DEV;
        }
        break;
    default:
        //Should never occur
        abort();
    }
}

static void action_open_dev(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_addr != 0);
    Serial.println( "Opening device at address " + String(driver_obj->dev_addr));
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl, 0, 0);
    //Get the device's information next
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    Serial.println( "Getting device information\r\n");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    Serial.println( "\t" + String((dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full") + " speed");
    Serial.println( "\tbConfigurationValue " + String(dev_info.bConfigurationValue));

    //Get the device descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    Serial.println( "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    //Get the device's config descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    Serial.println( "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    //Get the device's string descriptors next
    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        Serial.println( "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        Serial.println( "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        Serial.println( "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    //Nothing to do until the device disconnects
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
    driver_obj->actions |= ACTION_SEND_PRING_COMMAND;
}

static void aciton_close_dev(class_driver_t *driver_obj)
{
    ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;
    //We need to exit the event handler loop
    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions |= ACTION_EXIT;
}

static void transfer_cb(usb_transfer_t *transfer)
{
    // This function is called from within usb_host_client_handle_events(). Do not block and try to keep it short
    struct class_driver_control *class_driver_obj = (struct class_driver_control *)transfer->context;
    Serial.println("Transfer status " + String(transfer->status) + ", actual number of bytes transferred " + String(transfer->actual_num_bytes));
}

void aciton_send_print_command(class_driver_t *driver_obj){

    Serial.println( "Sending data to USB... ");

    usb_transfer_t *transfer;
    usb_host_transfer_alloc(64, 0, &transfer);

    // Send an OUT transfer to EP1
    memset(transfer->data_buffer, 0xAA, 64);


    char cmd_initialize_printer[] = {0x1b, 0x40};
    char cmd_unknown2[] = {0x1b, 0x52, 0x00};
    char cmd_unknown3[] = {0x1b, 0x63, 0x33, 0x04, 0x0d};
    char cmd_char_size_normal[] = {0x1D, 0x21, 0x0};
    char cmd_char_font_B[] = {0x1b, 0x21, 0x01};
    char cmd_char_font_A[] = {0x1b, 0x21, 0x00};
    char cmd_set_empesis_bold[] = {0x1b, 0x45, 0x01};
    char cmd_set_empesis_normal[] = {0x1b, 0x45, 0x00};
    char cmd_set_fontB_double_height[] = {0x1b, 0x21, 0x11};
    char cmd_set_fontA_double_height[] = {0x1b, 0x21, 0x10};
    char cmd_cut_paper[] = {0x1b, 0x69};
    char cmd_beep[] = { 0x1B, 0x28, 0x41, 0x04, 0x00, 0x01, 0x01, 0x01 };

    int idx = 0;
    memcpy(&(transfer->data_buffer[idx]), &cmd_initialize_printer, sizeof(cmd_initialize_printer));
    idx += sizeof(cmd_initialize_printer);
    memcpy(&(transfer->data_buffer[idx]), &cmd_unknown2, sizeof(cmd_unknown2));
    idx += sizeof(cmd_unknown2);
    memcpy(&(transfer->data_buffer[idx]), &cmd_unknown3, sizeof(cmd_unknown3));
    idx += sizeof(cmd_unknown3);
    memcpy(&(transfer->data_buffer[idx]), &cmd_char_size_normal, sizeof(cmd_char_size_normal));
    idx += sizeof(cmd_char_size_normal);
    memcpy(&(transfer->data_buffer[idx]), &cmd_char_font_B, sizeof(cmd_char_font_B));
    idx += sizeof(cmd_char_font_B);

    sprintf((char *)(&(transfer->data_buffer[idx])), "Hello World !!! \r\n");


    transfer->num_bytes = 64;
    transfer->device_handle = driver_obj->dev_hdl;
    transfer->bEndpointAddress = 0x01;
    transfer->callback = transfer_cb;
    transfer->context = (void *)&driver_obj;
    usb_host_transfer_submit(transfer);

    driver_obj->actions &= ~ACTION_SEND_PRING_COMMAND;
}

static void action_send_print_data(class_driver_t *driver_obj)
{
    esp_err_t err;
    usb_transfer_t *transfer;
    const size_t data_len = usb_round_up_to_mps(driver_obj->print_len, 64);

    Serial.println( "Sending data to USB... ");

    usb_host_transfer_alloc(data_len, 0, &transfer);
    memset(transfer->data_buffer, 0xAA, data_len);
    memcpy(transfer->data_buffer, driver_obj->printer_data, driver_obj->print_len);

    //clear data
    driver_obj->print_len = 0;
    free(driver_obj->printer_data);

    //set parameters in transfer
    transfer->num_bytes = data_len;
    transfer->device_handle = driver_obj->dev_hdl;
    transfer->bEndpointAddress = 0x01;
    transfer->callback = transfer_cb;
    transfer->context = (void *)&driver_obj;
    
    err = usb_host_transfer_submit(transfer);
    if(err != ESP_OK)
    {
        Serial.println( "Sending data FAILED! err:" +  String(err));
    }
    else
    {
        Serial.println( "Sending data done!");
    }
    
    driver_obj->actions &= ~ACTION_SEND_PRING_COMMAND;
}

void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

    //Wait until daemon task has installed USB Host Library
    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    Serial.println( "Registering Client");
    usb_host_client_config_t client_config = {
        .is_synchronous = false,    //Synchronous clients currently not supported. Set this to false
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));

    while (1) {
        if (driver_obj.actions == 0) {
            usb_host_client_handle_events(driver_obj.client_hdl, portMAX_DELAY);
        } else {
            if (driver_obj.actions & ACTION_OPEN_DEV) {
                action_open_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_INFO) {
                action_get_info(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_DESC) {
                action_get_dev_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_CONFIG_DESC) {
                action_get_config_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_STR_DESC) {
                action_get_str_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_CLOSE_DEV) {
                //aciton_close_dev(&driver_obj);
            }
            if(driver_obj.actions & ACTION_SEND_PRING_COMMAND){
                // aciton_send_print_command(&driver_obj);
                action_send_print_data(&driver_obj);
            }
            if (driver_obj.actions & ACTION_EXIT) {
                break;
            }
        }
    }

    Serial.println( "Deregistering Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));

    //Wait to be deleted
    xSemaphoreGive(signaling_sem);
    vTaskSuspend(NULL);
}


void host_lib_daemon_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;
    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    Serial.println("Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    // const esp_err_t err = usb_host_install(&host_config);
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    // if(err != ESP_OK)
    // {
    //     Serial.println("err=" + String(err));
    //     while (1)
    //     {
    //         vTaskDelay(1);
    //     }
        
    // }
    // Serial.println( "usb host install OK");
    // Signal to the class driver task that the host library is installed
    xSemaphoreGive(signaling_sem);
    vTaskDelay(10); // Short delay to let client task spin up
    while (1)
    {
        bool has_clients = true;
        bool has_devices = true;
        while (has_clients || has_devices)
        {
            uint32_t event_flags;
            ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            {
                has_clients = false;
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
            {
                has_devices = false;
            }
        }
        Serial.println( "No more clients and devices");

        // xSemaphoreTake(signaling_sem, portMAX_DELAY);

        // // Uninstall the USB Host Library
        // ESP_ERROR_CHECK(usb_host_uninstall());
        // // Wait to be deleted
        // xSemaphoreGive(signaling_sem);
    }

    vTaskDelete(NULL);
}

void usb_printer_init()
{
    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(signaling_sem);
    xTaskCreatePinnedToCore(host_lib_daemon_task,
                            "daemon",
                            4096,
                            (void *)signaling_sem,
                            DAEMON_TASK_PRIORITY,
                            &daemon_task_hdl,
                            0);
    // Create the class driver task
    xTaskCreatePinnedToCore(class_driver_task,
                            "class",
                            4096,
                            (void *)signaling_sem,
                            CLASS_TASK_PRIORITY,
                            &class_driver_task_hdl,
                            0);
    Serial.println("USB Printer started");
}

bool usb_printer_connected()
{
    return (bool)driver_obj.dev_hdl;
}

void usb_printer_send_data(const uint8_t *data, size_t len)
{
    //skip if no dev connected or input data is invalid
    if ( driver_obj.dev_hdl == NULL || data == NULL || len == 0 )
    {
        return;
    }

    while (driver_obj.print_len)
    {
        vTaskDelay(1); //wait until lenght becoms zero
    }
    //free memory if still is there
    if(driver_obj.printer_data)
        free(driver_obj.printer_data);
    
    driver_obj.printer_data = (uint8_t*)malloc(len);

    if(driver_obj.printer_data == NULL)
        return;

    driver_obj.print_len = len; //set packet length

    memcpy(driver_obj.printer_data, data, len);

    // set flag to USB task
    driver_obj.actions |= ACTION_SEND_PRING_COMMAND;
}