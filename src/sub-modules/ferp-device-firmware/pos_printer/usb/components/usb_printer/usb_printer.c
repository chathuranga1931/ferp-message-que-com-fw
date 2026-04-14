#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "usb_printer.h"

#define DAEMON_TASK_PRIORITY 7
#define CLASS_TASK_PRIORITY 8

#define CLIENT_NUM_EVENT_MSG 5

#define ACTION_OPEN_DEV BIT0        // 0x01
#define ACTION_GET_DEV_INFO BIT1    // 0x02
#define ACTION_GET_DEV_DESC BIT2    // 0x04
#define ACTION_GET_CONFIG_DESC BIT3 // 0x08
#define ACTION_GET_STR_DESC BIT4    // 0x10
#define ACTION_CLOSE_DEV BIT5       // 0x20
#define ACTION_EXIT BIT6            // 0x40

#define ESP_ERROR_GOTO(ret, line, func) \
    ret = func;                         \
    if (ret != ESP_OK)                  \
        goto line;

typedef struct
{
    usb_device_handle_t dev_hdl;
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    uint32_t actions;
} class_driver_t;

SemaphoreHandle_t signaling_sem = NULL;
TaskHandle_t daemon_task_hdl;
TaskHandle_t class_driver_task_hdl;
class_driver_t driver_obj = {0};
static bool printer_connected = false;

static const char *TAG = "usb_printer";

void class_driver_task(void *arg);

void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event)
    {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (driver_obj->dev_addr == 0)
        {
            driver_obj->dev_addr = event_msg->new_dev.address;
            // Open the device next
            driver_obj->actions |= ACTION_OPEN_DEV;
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (driver_obj->dev_hdl != NULL)
        {
            // Cancel any other actions and close the device next
            driver_obj->actions = ACTION_CLOSE_DEV;
        }
        break;
    default:
        // Should never occur
        abort();
    }
}

void action_open_dev(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", driver_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    // Get the device's information next
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

void action_get_info(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);

    // Get the device descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

void action_get_dev_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    // Get the device's config descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

void action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    // Get the device's string descriptors next
    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

void action_get_str_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer)
    {
        ESP_LOGI(TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product)
    {
        ESP_LOGI(TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num)
    {
        ESP_LOGI(TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    // Nothing to do until the device disconnects
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
}

void aciton_close_dev(class_driver_t *driver_obj)
{
    ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;
    // We need to exit the event handler loop
    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions |= ACTION_EXIT;
}

void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

    while (1)
    {
        driver_obj = (class_driver_t){0};

        // Wait until daemon task has installed USB Host Library
        xSemaphoreTake(signaling_sem, portMAX_DELAY);

        ESP_LOGI(TAG, "Registering Client");
        usb_host_client_config_t client_config = {
            .is_synchronous = false, // Synchronous clients currently not supported. Set this to false
            .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
            .async = {
                .client_event_callback = client_event_cb,
                .callback_arg = (void *)&driver_obj,
            },
        };
        ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));

        while (1)
        {
            if (driver_obj.actions == 0)
            {
                usb_host_client_handle_events(driver_obj.client_hdl, portMAX_DELAY);
            }
            else
            {
                if (driver_obj.actions & ACTION_OPEN_DEV)
                {
                    action_open_dev(&driver_obj);
                }
                if (driver_obj.actions & ACTION_GET_DEV_INFO)
                {
                    action_get_info(&driver_obj);
                }
                if (driver_obj.actions & ACTION_GET_DEV_DESC)
                {
                    action_get_dev_desc(&driver_obj);
                }
                if (driver_obj.actions & ACTION_GET_CONFIG_DESC)
                {
                    action_get_config_desc(&driver_obj);
                }
                if (driver_obj.actions & ACTION_GET_STR_DESC)
                {
                    action_get_str_desc(&driver_obj);
                    ESP_LOGI(TAG, "Device connected");
                    printer_connected = true;
                }
                if (driver_obj.actions & ACTION_CLOSE_DEV)
                {
                    printer_connected = false;
                    aciton_close_dev(&driver_obj);
                }
                if (driver_obj.actions & ACTION_EXIT)
                {
                    break;
                }
            }
        }

        ESP_LOGI(TAG, "Deregistering Client");
        ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));

        // Wait to be deleted
        xSemaphoreGive(signaling_sem);

        vTaskDelay(10);
    }
    vTaskDelete(NULL);
}

void host_lib_daemon_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;
    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

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
        ESP_LOGI(TAG, "No more clients and devices");

        // xSemaphoreTake(signaling_sem, portMAX_DELAY);

        // // Uninstall the USB Host Library
        // ESP_ERROR_CHECK(usb_host_uninstall());
        // // Wait to be deleted
        // xSemaphoreGive(signaling_sem);
    }

    vTaskDelete(NULL);
}

esp_err_t esc_pos_send(const char *buff, const size_t len)
{
    esp_err_t ret = ESP_OK;
    usb_transfer_t *trans = NULL;
    ESP_ERROR_GOTO(ret, end, usb_host_transfer_alloc(len, 0, &trans));
    ESP_LOGI(__func__, "transfer alloc");
    trans->device_handle = driver_obj.dev_hdl;
    trans->bEndpointAddress = 1;

    memcpy(trans->data_buffer, buff, len);
    trans->num_bytes = len;
    ret = usb_host_transfer_submit(trans);
    ESP_LOGI(__func__, "transfer submit:0x%.2x", ret);
    usb_host_transfer_free(trans);
end:
    return ret;
}

static void printer_task(void *arg)
{
#define ESC_INIT "\x1B\x40"             // Initialize Printer
#define ESC_ALIGN_CENTER "\x1B\x61\x01"  // Center Alignment
#define ESC_FEED "\x0A"                 // Line Feed

    esp_err_t err = ESP_OK;
    while (1)
    {
        // waiting for the printer
        while (!printer_connected)
        {
            vTaskDelay(1);
        }
        ESP_LOGI(__func__, "Printer Connected");

        // Open interfaces and endpoints
        err = usb_host_interface_claim(driver_obj.client_hdl, driver_obj.dev_hdl, 0, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "usb_host_interface_claim failed");
            goto unplug;
        }

        // // Send initialization command
        // if(esc_pos_send(ESC_INIT, sizeof(ESC_INIT) - 1) != ESP_OK)
        //     ESP_LOGW(TAG, "Failed to initialize printer");
        // else
        //     ESP_LOGI(TAG, "Initialization command sent");

        // // Center align text
        // if(esc_pos_send(ESC_ALIGN_CENTER, sizeof(ESC_ALIGN_CENTER) - 1) != ESP_OK) 
        //     ESP_LOGW(TAG, "Failed to set alignment");

        // // Print text
        // const char *text = "Hello, ESC/POS Printer!\n";
        // if(esc_pos_send(text, strlen(text)) != ESP_OK)
        //     ESP_LOGW(TAG, "Failed to print text");
        
        // // Feed paper
        // if(esc_pos_send(ESC_FEED, sizeof(ESC_FEED) - 1) != ESP_OK)
        //     ESP_LOGI(TAG, "Failed to feed paper");
        
        usb_host_interface_release(driver_obj.client_hdl, driver_obj.dev_hdl, 0);
    unplug:
        // waiting for the printer
        while (printer_connected)
        {
            vTaskDelay(1);
        }
        ESP_LOGI(__func__, "Printer dis-connected");
    }
    vTaskDelete(NULL);
}

void usb_printer_init(void)
{
    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(signaling_sem);
    // Create daemon task
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

    xTaskCreate(printer_task, "printer_task", 4 * 1024, NULL, 10, NULL);

    
}
