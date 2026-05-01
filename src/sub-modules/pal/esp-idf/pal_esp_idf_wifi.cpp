/**
 * @file pal_esp_idf_wifi.cpp
 * @brief ESP-IDF implementation of WiFi PAL
 * 
 * This implementation uses ESP-IDF's esp_wifi and esp_netif components
 * to provide WiFi functionality for ESP32 chips.
 */

#include "pal_wifi.h"
#include "pal_logger.h"

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#define __TAG__ "PAL_WIFI"

#define P_WIFI_DEBUG_LOG_EN      LOG_DIS

// ============================================================================
// Static Variables
// ============================================================================

static bool s_wifi_initialized = false;
static bool s_netif_initialized = false;
static pal_wifi_mode_t s_current_mode = PAL_WIFI_MODE_NULL;
static pal_wifi_event_callback_t s_event_callback = NULL;
static void* s_user_data = NULL;

static esp_netif_t* s_netif_sta = NULL;
static esp_netif_t* s_netif_ap = NULL;

static bool s_sta_connected = false;
static int8_t s_sta_rssi = -127;

// ============================================================================
// Event Handler
// ============================================================================

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi Station started");
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_STA_START, NULL, s_user_data);
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Connected to AP SSID:%s channel:%d",
                        event->ssid, event->channel);
                s_sta_connected = true;
                
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_STA_CONNECTED, event_data, s_user_data);
                }
                break;
            }
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Disconnected from AP, reason:%d", event->reason);
                s_sta_connected = false;
                s_sta_rssi = -127;
                
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_STA_DISCONNECTED, event_data, s_user_data);
                }
                break;
            }
                
            case WIFI_EVENT_AP_START:
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi Access Point started");
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_AP_START, NULL, s_user_data);
                }
                break;
                
            case WIFI_EVENT_AP_STOP:
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi Access Point stopped");
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_AP_STOP, NULL, s_user_data);
                }
                break;
                
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Station " MACSTR " joined, AID=%d",
                        MAC2STR(event->mac), event->aid);
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_AP_STACONNECTED, event_data, s_user_data);
                }
                break;
            }
                
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Station " MACSTR " left, AID=%d",
                        MAC2STR(event->mac), event->aid);
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_AP_STADISCONNECTED, event_data, s_user_data);
                }
                break;
            }
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                
                if (s_event_callback) {
                    s_event_callback(PAL_WIFI_EVENT_STA_GOT_IP, event_data, s_user_data);
                }
                break;
            }
                
            default:
                break;
        }
    }
}

// ============================================================================
// WiFi Management Functions
// ============================================================================

int32_t pal_wifi_init(const pal_wifi_init_config_t* config,
                      pal_wifi_event_callback_t event_callback,
                      void* user_data) {
    
    if (config == NULL) {
        LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Config is NULL");
        return -1;
    }
    
    if (s_wifi_initialized) {
        LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi already initialized");
        return 0;
    }
    
    // Store callback and user data
    s_event_callback = event_callback;
    s_user_data = user_data;
    s_current_mode = config->mode;
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "NVS partition was truncated and needs to be erased");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Failed to erase NVS: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize network interface if not already done.
    // esp_netif_init() is called once at startup from pal_system_init() before
    // tasks start; the guard here prevents a second init if WiFi is reconfigured.
    if (!s_netif_initialized) {
        // esp_netif_init() is idempotent in IDF 5.x (reference-counted).
        esp_netif_init();

        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE means already created by pal_system_init(), which is OK
            LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Failed to create event loop: %s", esp_err_to_name(ret));
            return ret;
        }
        s_netif_initialized = true;
    }
    
    // Create network interfaces based on mode
    if (config->mode == PAL_WIFI_MODE_STA || config->mode == PAL_WIFI_MODE_APSTA) {
        if (s_netif_sta == NULL) {
            s_netif_sta = esp_netif_create_default_wifi_sta();
        }
    }
    
    if (config->mode == PAL_WIFI_MODE_AP || config->mode == PAL_WIFI_MODE_APSTA) {
        if (s_netif_ap == NULL) {
            s_netif_ap = esp_netif_create_default_wifi_ap();
        }
    }
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    PAL_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    PAL_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    PAL_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // Set WiFi mode
    wifi_mode_t esp_mode;
    switch (config->mode) {
        case PAL_WIFI_MODE_STA:
            esp_mode = WIFI_MODE_STA;
            break;
        case PAL_WIFI_MODE_AP:
            esp_mode = WIFI_MODE_AP;
            break;
        case PAL_WIFI_MODE_APSTA:
            esp_mode = WIFI_MODE_APSTA;
            break;
        default:
            LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Invalid WiFi mode");
            return -1;
    }
    PAL_ERROR_CHECK(esp_wifi_set_mode(esp_mode));
    
    // Configure WiFi based on mode
    wifi_config_t wifi_config = {};
    
    if (config->mode == PAL_WIFI_MODE_STA || config->mode == PAL_WIFI_MODE_APSTA) {
        // Station mode configuration
        strncpy((char*)wifi_config.sta.ssid, config->config.sta.ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, config->config.sta.password, sizeof(wifi_config.sta.password) - 1);
        
        if (config->config.sta.bssid_set) {
            wifi_config.sta.bssid_set = true;
            memcpy(wifi_config.sta.bssid, config->config.sta.bssid, 6);
        }
        
        if (config->config.sta.channel > 0) {
            wifi_config.sta.channel = config->config.sta.channel;
        }
        
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        PAL_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    
    if (config->mode == PAL_WIFI_MODE_AP || config->mode == PAL_WIFI_MODE_APSTA) {
        // Access Point mode configuration
        memset(&wifi_config, 0, sizeof(wifi_config));
        
        strncpy((char*)wifi_config.ap.ssid, config->config.ap.ssid, sizeof(wifi_config.ap.ssid) - 1);
        wifi_config.ap.ssid_len = config->config.ap.ssid_len > 0 ? 
                                  config->config.ap.ssid_len : 
                                  strlen(config->config.ap.ssid);
        
        strncpy((char*)wifi_config.ap.password, config->config.ap.password, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.channel = config->config.ap.channel > 0 ? config->config.ap.channel : 1;
        wifi_config.ap.max_connection = config->config.ap.max_connections > 0 ? 
                                       config->config.ap.max_connections : 4;
        
        // Set authentication mode
        switch (config->config.ap.auth_mode) {
            case PAL_WIFI_AUTH_OPEN:
                wifi_config.ap.authmode = WIFI_AUTH_OPEN;
                break;
            case PAL_WIFI_AUTH_WPA_PSK:
                wifi_config.ap.authmode = WIFI_AUTH_WPA_PSK;
                break;
            case PAL_WIFI_AUTH_WPA2_PSK:
                wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
                break;
            case PAL_WIFI_AUTH_WPA_WPA2_PSK:
                wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
                break;
            default:
                wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
                break;
        }
        
        // If password is empty, use open mode
        if (strlen(config->config.ap.password) == 0) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }
        
        wifi_config.ap.ssid_hidden = config->config.ap.ssid_hidden ? 1 : 0;
        
        PAL_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    }
    
    s_wifi_initialized = true;
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi initialized successfully");
    
    return 0;
}

int32_t pal_wifi_deinit(void) {
    if (!s_wifi_initialized) {
        return 0;
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    if (s_netif_sta) {
        esp_netif_destroy(s_netif_sta);
        s_netif_sta = NULL;
    }
    
    if (s_netif_ap) {
        esp_netif_destroy(s_netif_ap);
        s_netif_ap = NULL;
    }
    
    s_wifi_initialized = false;
    s_sta_connected = false;
    s_sta_rssi = -127;
    
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi deinitialized");
    
    return 0;
}

int32_t pal_wifi_start(void) {
    if (!s_wifi_initialized) {
        LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "WiFi not initialized");
        return -1;
    }
    
    PAL_ERROR_CHECK(esp_wifi_start());
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi started");
    
    return 0;
}

int32_t pal_wifi_stop(void) {
    if (!s_wifi_initialized) {
        return 0;
    }
    
    PAL_ERROR_CHECK(esp_wifi_stop());
    s_sta_connected = false;
    
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "WiFi stopped");
    
    return 0;
}

// ============================================================================
// Station Mode Functions
// ============================================================================

int32_t pal_wifi_sta_connect(void) {
    if (!s_wifi_initialized) {
        LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "WiFi not initialized");
        return -1;
    }
    
    if (s_current_mode != PAL_WIFI_MODE_STA && s_current_mode != PAL_WIFI_MODE_APSTA) {
        LOG_MSG_ERROR(P_WIFI_DEBUG_LOG_EN, "Not in Station mode");
        return -1;
    }
    
    PAL_ERROR_CHECK(esp_wifi_connect());
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Connecting to WiFi...");
    
    return 0;
}

int32_t pal_wifi_sta_disconnect(void) {
    if (!s_wifi_initialized) {
        return 0;
    }
    
    if (s_current_mode != PAL_WIFI_MODE_STA && s_current_mode != PAL_WIFI_MODE_APSTA) {
        return -1;
    }
    
    PAL_ERROR_CHECK(esp_wifi_disconnect());
    s_sta_connected = false;
    
    LOG_MSG_DEBUG(P_WIFI_DEBUG_LOG_EN, "Disconnected from WiFi");
    
    return 0;
}

bool pal_wifi_sta_is_connected(void) {
    return s_sta_connected;
}

int32_t pal_wifi_sta_get_rssi(int8_t* rssi) {
    if (rssi == NULL) {
        return -1;
    }
    
    if (!s_sta_connected) {
        *rssi = -127;
        return -1;
    }
    
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (ret == ESP_OK) {
        s_sta_rssi = ap_info.rssi;
        *rssi = ap_info.rssi;
        return 0;
    }
    
    *rssi = -127;
    return -1;
}

// ============================================================================
// Access Point Mode Functions
// ============================================================================

int32_t pal_wifi_ap_get_sta_count(uint8_t* num_sta) {
    if (num_sta == NULL) {
        return -1;
    }
    
    if (!s_wifi_initialized) {
        *num_sta = 0;
        return -1;
    }
    
    if (s_current_mode != PAL_WIFI_MODE_AP && s_current_mode != PAL_WIFI_MODE_APSTA) {
        *num_sta = 0;
        return -1;
    }
    
    wifi_sta_list_t sta_list;
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);
    
    if (ret == ESP_OK) {
        *num_sta = sta_list.num;
        return 0;
    }
    
    *num_sta = 0;
    return -1;
}

// ============================================================================
// Common Information Functions
// ============================================================================

int32_t pal_wifi_get_mac(uint8_t mac[6]) {
    if (mac == NULL) {
        return -1;
    }
    
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    
    wifi_interface_t iface = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) ? 
                             WIFI_IF_STA : WIFI_IF_AP;
    
    esp_err_t ret = esp_wifi_get_mac(iface, mac);
    
    return (ret == ESP_OK) ? 0 : -1;
}

int32_t pal_wifi_get_mac_str(char* mac_str, size_t max_len) {
    if (mac_str == NULL || max_len < PAL_WIFI_MAC_STR_LEN) {
        return -1;
    }
    
    uint8_t mac[6];
    if (pal_wifi_get_mac(mac) != 0) {
        return -1;
    }
    
    snprintf(mac_str, max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return 0;
}

int32_t pal_wifi_get_ip_str(char* ip_str, size_t max_len) {
    if (ip_str == NULL || max_len < PAL_WIFI_IP_STR_LEN) {
        return -1;
    }
    
    esp_netif_t* netif = NULL;
    
    if (s_current_mode == PAL_WIFI_MODE_STA || s_current_mode == PAL_WIFI_MODE_APSTA) {
        netif = s_netif_sta;
    } else if (s_current_mode == PAL_WIFI_MODE_AP) {
        netif = s_netif_ap;
    }
    
    if (netif == NULL) {
        strncpy(ip_str, "0.0.0.0", max_len);
        return -1;
    }
    
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    
    if (ret == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return 0;
    }
    
    strncpy(ip_str, "0.0.0.0", max_len);
    return -1;
}

int32_t pal_wifi_get_status(pal_wifi_status_t* status) {
    if (status == NULL) {
        return -1;
    }
    
    memset(status, 0, sizeof(pal_wifi_status_t));
    
    status->is_connected = s_sta_connected;
    
    if (s_sta_connected) {
        pal_wifi_sta_get_rssi(&status->rssi);
        
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            status->channel = ap_info.primary;
        }
    }
    
    pal_wifi_get_ip_str(status->ip_addr, sizeof(status->ip_addr));
    pal_wifi_get_mac_str(status->mac_addr, sizeof(status->mac_addr));
    
    return 0;
}

int32_t pal_wifi_get_mode(pal_wifi_mode_t* mode) {
    if (mode == NULL) {
        return -1;
    }
    
    *mode = s_current_mode;
    return 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

uint8_t pal_wifi_rssi_to_level(int8_t rssi, uint8_t num_levels) {
    if (num_levels == 0) {
        return 0;
    }
    
    // Define RSSI range: -100 dBm (worst) to -50 dBm (best)
    const int8_t rssi_min = -100;
    const int8_t rssi_max = -50;
    
    // Clamp RSSI to range
    if (rssi < rssi_min) rssi = rssi_min;
    if (rssi > rssi_max) rssi = rssi_max;
    
    // Calculate level (0 to num_levels-1)
    int32_t range = rssi_max - rssi_min;
    int32_t level = ((int32_t)(rssi - rssi_min) * (num_levels - 1)) / range;
    
    // Ensure within bounds
    if (level < 0) level = 0;
    if (level >= num_levels) level = num_levels - 1;
    
    return (uint8_t)level;
}
