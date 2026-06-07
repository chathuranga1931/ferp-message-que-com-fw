/**
 * @file mac_cmd_distap.cpp
 * @brief Simulator implementation of cmd_distap.h (cmd_distap.c on ESP-IDF).
 *
 * All functions return success immediately.
 * distap_get_fw_version() returns "SIM_1.0.0" so FuelDispTapDriver's version
 * check always passes and no flash update is ever triggered.
 */

#include "cmd_distap.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__      "MAC_CMD "
#define MAC_CMD_LOG  false   // quiet by default — set true to trace lifecycle

#define MLOG(fmt, ...)  LOG_MSG_INFO( MAC_CMD_LOG, fmt, ##__VA_ARGS__)

esp_err_t distap_get_fw_version(char *ver)
{
    if (ver) strncpy(ver, "SIM_1.0.0", 32);
    MLOG("distap_get_fw_version -> \"SIM_1.0.0\"");
    return ESP_OK;
}

esp_err_t distap_get_fw_name()            { return ESP_OK; }
esp_err_t distap_get_fw_timedate()        { return ESP_OK; }

esp_err_t distap_set_display_type(const display_type_t type)
{
    MLOG("distap_set_display_type(%d)", (int)type);
    return ESP_OK;
}

esp_err_t distap_get_inputs(input_pin_t *pins)
{
    if (pins) pins->u32int = 0;
    return ESP_OK;
}

esp_err_t distap_set_inputenable(bool /*level*/)  { return ESP_OK; }
esp_err_t distap_set_led_enable (bool /*level*/)  { return ESP_OK; }
esp_err_t distap_set_cs1_enable (bool /*level*/)  { return ESP_OK; }
esp_err_t distap_set_cs2_enable (bool /*level*/)  { return ESP_OK; }

esp_err_t distap_set_err_mask(const data_error_t err)
{
    MLOG("distap_set_err_mask(0x%02X)", (unsigned)err.u8int);
    return ESP_OK;
}
