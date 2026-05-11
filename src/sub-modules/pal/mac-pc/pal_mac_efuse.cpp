// pal_mac_efuse.cpp
//
// Simulator implementation of pal_efuse.h.
// MAC is hardcoded to match the IDF eFuse value for the target hardware.

#include "pal_efuse.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__          "PAL_EFUS"
#define EFUS_LOG_EN      false

// Hardcoded MAC — matches pal_esp_idf_efuse.cpp: 11:22:33:44:55:66
static const uint8_t k_sim_mac[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

int32_t pal_efuse_get_mac(uint8_t *mac, size_t length)
{
    if (mac == nullptr || length < 6) {
        return PAL_ERROR_INVALID;
    }
    memcpy(mac, k_sim_mac, 6);
    LOG_MSG_INFO(EFUS_LOG_EN, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return PAL_OK;
}

int32_t pal_efuse_get_chip_id(uint8_t *id, size_t *length)
{
    if (id == nullptr || length == nullptr || *length < 6) {
        return PAL_ERROR_INVALID;
    }
    memcpy(id, k_sim_mac, 6);
    *length = 6;
    return PAL_OK;
}
