#include "pal/pal_i2c.h"
#include <driver/i2c.h>
#include <esp_log.h>

static const char* TAG = "PAL_I2C";

// Convert PAL port to ESP-IDF port
static i2c_port_t convert_port(pal_i2c_port_t port) {
    switch(port) {
        case PAL_I2C_PORT_0: return I2C_NUM_0;
        case PAL_I2C_PORT_1: return I2C_NUM_1;
        default: return I2C_NUM_0;
    }
}

int32_t pal_i2c_init(pal_i2c_port_t port, const pal_i2c_config_t* config) {
    if (config == NULL) {
        return PAL_ERROR_INVALID;
    }

    i2c_config_t conf = {
        .mode = (config->mode == PAL_I2C_MODE_MASTER) ? I2C_MODE_MASTER : I2C_MODE_SLAVE,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = config->sda_pullup_enable ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .scl_pullup_en = config->scl_pullup_enable ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .master = {
            .clk_speed = config->clock_speed,
        },
    };

    i2c_port_t i2c_port = convert_port(port);
    
    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %d", ret);
        return PAL_ERROR;
    }

    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %d", ret);
        return PAL_ERROR;
    }

    ESP_LOGI(TAG, "I2C port %d initialized (SDA:%d SCL:%d @ %lu Hz)", 
             port, config->sda_pin, config->scl_pin, config->clock_speed);
    
    return PAL_OK;
}

int32_t pal_i2c_deinit(pal_i2c_port_t port) {
    i2c_port_t i2c_port = convert_port(port);
    
    esp_err_t ret = i2c_driver_delete(i2c_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver delete failed: %d", ret);
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

bool pal_i2c_device_probe(pal_i2c_port_t port, uint8_t device_address, uint32_t timeout_ms) {
    i2c_port_t i2c_port = convert_port(port);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    
    return (ret == ESP_OK);
}

int32_t pal_i2c_write(pal_i2c_port_t port, uint8_t device_address, 
                      const uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0) {
        return PAL_ERROR_INVALID;
    }

    i2c_port_t i2c_port = convert_port(port);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t*)data, length, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    
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

    i2c_port_t i2c_port = convert_port(port);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_address << 1) | I2C_MASTER_READ, true);
    
    if (length > 1) {
        i2c_master_read(cmd, data, length - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + length - 1, I2C_MASTER_NACK);
    
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    
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

    i2c_port_t i2c_port = convert_port(port);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // Write phase
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t*)write_data, write_length, true);
    
    // Read phase (repeated start)
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_address << 1) | I2C_MASTER_READ, true);
    
    if (read_length > 1) {
        i2c_master_read(cmd, read_data, read_length - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, read_data + read_length - 1, I2C_MASTER_NACK);
    
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write-read failed: %d", ret);
        return PAL_ERROR;
    }
    
    return PAL_OK;
}
