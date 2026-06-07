#include "pal/pal_efuse.h"
#include "pal/pal_logger.h"
#include <esp_mac.h>
#include <esp_system.h>
#include <string.h>

#define __TAG__ "PAL_EFUS"

#define EFUS_DEBUG_LOG_EN      LOG_DIS

int32_t pal_efuse_get_mac(uint8_t* mac, size_t length) {
    
    if (mac == NULL) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "MAC buffer is NULL");
        return PAL_ERROR_INVALID;
    }

    if(length < 6) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "MAC buffer too small, need at least 6 bytes");
        return PAL_ERROR_INVALID;
    }

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "Failed to get MAC address: %d", ret);
        return PAL_ERROR;
    }

#if defined(TEST_PUMP_BUILD)
    // //112233445566
    mac[0] = 0x11;
    mac[1] = 0x22;
    mac[2] = 0x33;
    mac[3] = 0x44;
    mac[4] = 0x55;
    mac[5] = 0x66;
#endif

    LOG_MSG_INFO(EFUS_DEBUG_LOG_EN, "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return PAL_OK;
}

int32_t pal_efuse_get_chip_id(uint8_t* id, size_t* length) {
    if (id == NULL || length == NULL) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "Invalid parameters");
        return PAL_ERROR_INVALID;
    }

    // ESP32 uses MAC address as unique chip ID (6 bytes)
    // But we'll read the full 8-byte version if buffer is large enough
    if (*length < 6) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "Buffer too small, need at least 6 bytes");
        return PAL_ERROR_INVALID;
    }

    uint8_t mac[8] = {0};
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        LOG_MSG_ERROR(EFUS_DEBUG_LOG_EN, "Failed to get chip ID: %d", ret);
        return PAL_ERROR;
    }


    // 112233445566
    mac[0] = 0x11;
    mac[1] = 0x22;
    mac[2] = 0x33;
    mac[3] = 0x44;
    mac[4] = 0x55;
    mac[5] = 0x66;
    mac[6] = 0x00;
    mac[7] = 0x00;

    // Copy available bytes
    size_t copy_len = (*length >= 8) ? 8 : 6;
    memcpy(id, mac, copy_len);
    *length = copy_len;

    LOG_MSG_INFO(EFUS_DEBUG_LOG_EN, "Chip ID (%zu bytes): %02X%02X%02X%02X%02X%02X",
             copy_len, id[0], id[1], id[2], id[3], id[4], id[5]);

    return PAL_OK;
}
