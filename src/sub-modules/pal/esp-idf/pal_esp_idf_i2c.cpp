#include "pal/pal_i2c.h"
#include "driver/i2c_master.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "PAL_I2C";

#define PAL_I2C_MAX_DEVICES 4

static i2c_master_bus_handle_t s_bus[2]        = {NULL, NULL};
static uint32_t                s_bus_speed[2]  = {100000, 100000};

typedef struct {
    uint8_t               port_idx;
    uint8_t               addr;
    i2c_master_dev_handle_t handle;
} dev_entry_t;

static dev_entry_t s_devs[PAL_I2C_MAX_DEVICES];
static int         s_dev_count = 0;

static int port_idx(pal_i2c_port_t port) {
    return (port == PAL_I2C_PORT_0) ? 0 : 1;
}

static i2c_master_dev_handle_t get_dev(pal_i2c_port_t port, uint8_t addr) {
    int idx = port_idx(port);
    for (int i = 0; i < s_dev_count; i++) {
        if (s_devs[i].port_idx == idx && s_devs[i].addr == addr)
            return s_devs[i].handle;
    }
    if (s_dev_count >= PAL_I2C_MAX_DEVICES || !s_bus[idx]) return NULL;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = addr;
    dev_cfg.scl_speed_hz    = s_bus_speed[idx];

    i2c_master_dev_handle_t handle;
    if (i2c_master_bus_add_device(s_bus[idx], &dev_cfg, &handle) != ESP_OK) return NULL;

    s_devs[s_dev_count].port_idx = (uint8_t)idx;
    s_devs[s_dev_count].addr     = addr;
    s_devs[s_dev_count].handle   = handle;
    s_dev_count++;
    return handle;
}

int32_t pal_i2c_init(pal_i2c_port_t port, const pal_i2c_config_t* config) {
    if (config == NULL) {
        return PAL_ERROR_INVALID;
    }

    int idx = port_idx(port);
    s_bus_speed[idx] = config->clock_speed > 0 ? config->clock_speed : 100000;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port                  = (i2c_port_num_t)idx;
    bus_cfg.sda_io_num                = (gpio_num_t)config->sda_pin;
    bus_cfg.scl_io_num                = (gpio_num_t)config->scl_pin;
    bus_cfg.clk_source                = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt         = 7;
    bus_cfg.flags.enable_internal_pullup =
        config->sda_pullup_enable || config->scl_pullup_enable;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus[idx]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %d", ret);
        return PAL_ERROR;
    }

    ESP_LOGI(TAG, "I2C port %d initialized (SDA:%d SCL:%d @ %lu Hz)",
             port, config->sda_pin, config->scl_pin, config->clock_speed);
    return PAL_OK;
}

int32_t pal_i2c_deinit(pal_i2c_port_t port) {
    int idx = port_idx(port);
    if (!s_bus[idx]) return PAL_OK;

    i2c_del_master_bus(s_bus[idx]);
    s_bus[idx] = NULL;

    // Remove cached device entries for this port
    int w = 0;
    for (int r = 0; r < s_dev_count; r++) {
        if (s_devs[r].port_idx != idx)
            s_devs[w++] = s_devs[r];
    }
    s_dev_count = w;
    return PAL_OK;
}

bool pal_i2c_device_probe(pal_i2c_port_t port, uint8_t device_address, uint32_t timeout_ms) {
    int idx = port_idx(port);
    if (!s_bus[idx]) return false;
    return i2c_master_probe(s_bus[idx], device_address, (int)timeout_ms) == ESP_OK;
}

int32_t pal_i2c_write(pal_i2c_port_t port, uint8_t device_address,
                      const uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0) {
        return PAL_ERROR_INVALID;
    }
    i2c_master_dev_handle_t dev = get_dev(port, device_address);
    if (!dev) return PAL_ERROR;

    esp_err_t ret = i2c_master_transmit(dev, data, length, (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %d", ret);
        return PAL_ERROR;
    }
    return PAL_OK;
}

int32_t pal_i2c_read(pal_i2c_port_t port, uint8_t device_address,
                     uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0) {
        return PAL_ERROR_INVALID;
    }
    i2c_master_dev_handle_t dev = get_dev(port, device_address);
    if (!dev) return PAL_ERROR;

    esp_err_t ret = i2c_master_receive(dev, data, length, (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", ret);
        return PAL_ERROR;
    }
    return PAL_OK;
}

int32_t pal_i2c_write_read(pal_i2c_port_t port, uint8_t device_address,
                           const uint8_t* write_data, size_t write_length,
                           uint8_t* read_data, size_t read_length,
                           uint32_t timeout_ms) {
    if (write_data == NULL || write_length == 0 || read_data == NULL || read_length == 0) {
        return PAL_ERROR_INVALID;
    }
    i2c_master_dev_handle_t dev = get_dev(port, device_address);
    if (!dev) return PAL_ERROR;

    esp_err_t ret = i2c_master_transmit_receive(dev, write_data, write_length,
                                                read_data, read_length, (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write-read failed: %d", ret);
        return PAL_ERROR;
    }
    return PAL_OK;
}
