/**
 * @file pal_wifi.h
 * @brief Platform Abstraction Layer for WiFi operations
 * 
 * This header provides a platform-independent interface for WiFi functionality
 * including Station (STA) and Access Point (AP) modes, connection management,
 * and status monitoring.
 */

#ifndef PAL_WIFI_H
#define PAL_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define PAL_WIFI_SSID_MAX_LEN       32
#define PAL_WIFI_PASSWORD_MAX_LEN   64
#define PAL_WIFI_MAC_STR_LEN        18  // "XX:XX:XX:XX:XX:XX\0"
#define PAL_WIFI_IP_STR_LEN         16  // "XXX.XXX.XXX.XXX\0"

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief WiFi operating mode
 */
typedef enum {
    PAL_WIFI_MODE_NULL = 0,     /**< WiFi disabled */
    PAL_WIFI_MODE_STA,           /**< Station mode (client) */
    PAL_WIFI_MODE_AP,            /**< Access Point mode */
    PAL_WIFI_MODE_APSTA,         /**< Station + Access Point mode */
} pal_wifi_mode_t;

/**
 * @brief WiFi event types
 */
typedef enum {
    PAL_WIFI_EVENT_STA_START,           /**< Station started */
    PAL_WIFI_EVENT_STA_CONNECTED,       /**< Connected to AP */
    PAL_WIFI_EVENT_STA_DISCONNECTED,    /**< Disconnected from AP */
    PAL_WIFI_EVENT_STA_GOT_IP,          /**< Got IP address */
    PAL_WIFI_EVENT_AP_START,            /**< Access Point started */
    PAL_WIFI_EVENT_AP_STOP,             /**< Access Point stopped */
    PAL_WIFI_EVENT_AP_STACONNECTED,     /**< Station connected to AP */
    PAL_WIFI_EVENT_AP_STADISCONNECTED,  /**< Station disconnected from AP */
} pal_wifi_event_t;

/**
 * @brief WiFi authentication mode
 */
typedef enum {
    PAL_WIFI_AUTH_OPEN = 0,      /**< Open (no security) */
    PAL_WIFI_AUTH_WEP,           /**< WEP */
    PAL_WIFI_AUTH_WPA_PSK,       /**< WPA-PSK */
    PAL_WIFI_AUTH_WPA2_PSK,      /**< WPA2-PSK */
    PAL_WIFI_AUTH_WPA_WPA2_PSK,  /**< WPA/WPA2-PSK */
    PAL_WIFI_AUTH_WPA3_PSK,      /**< WPA3-PSK */
} pal_wifi_auth_mode_t;

/**
 * @brief WiFi configuration for Station mode
 */
typedef struct {
    char ssid[PAL_WIFI_SSID_MAX_LEN];           /**< SSID of target AP */
    char password[PAL_WIFI_PASSWORD_MAX_LEN];   /**< Password for target AP */
    bool bssid_set;                              /**< Whether to use specific BSSID */
    uint8_t bssid[6];                            /**< BSSID of target AP (if bssid_set) */
    uint8_t channel;                             /**< Channel of target AP (0 = auto) */
} pal_wifi_sta_config_t;

/**
 * @brief WiFi configuration for Access Point mode
 */
typedef struct {
    char ssid[PAL_WIFI_SSID_MAX_LEN];           /**< SSID of AP */
    char password[PAL_WIFI_PASSWORD_MAX_LEN];   /**< Password for AP */
    uint8_t ssid_len;                            /**< Length of SSID (0 = auto) */
    uint8_t channel;                             /**< WiFi channel (1-13) */
    pal_wifi_auth_mode_t auth_mode;              /**< Authentication mode */
    uint8_t max_connections;                     /**< Max number of stations (1-10) */
    bool ssid_hidden;                            /**< Hide SSID */
} pal_wifi_ap_config_t;

/**
 * @brief WiFi initialization configuration
 */
typedef struct {
    pal_wifi_mode_t mode;                        /**< WiFi operating mode */
    union {
        pal_wifi_sta_config_t sta;               /**< Station mode config */
        pal_wifi_ap_config_t ap;                 /**< Access Point mode config */
    } config;
    uint8_t rssi_no_levels;
} pal_wifi_init_config_t;

/**
 * @brief WiFi status information
 */
typedef struct {
    bool is_connected;                           /**< Connection status (for STA mode) */
    int8_t rssi;                                 /**< Signal strength in dBm (for STA mode) */
    uint8_t channel;                             /**< Current channel */
    char ip_addr[PAL_WIFI_IP_STR_LEN];          /**< IP address string */
    char mac_addr[PAL_WIFI_MAC_STR_LEN];        /**< MAC address string */
} pal_wifi_status_t;

/**
 * @brief WiFi event callback function type
 * 
 * @param event WiFi event that occurred
 * @param event_data Event-specific data (can be NULL)
 * @param user_data User data pointer passed during initialization
 */
typedef void (*pal_wifi_event_callback_t)(pal_wifi_event_t event, void* event_data, void* user_data);

// ============================================================================
// WiFi Management Functions
// ============================================================================

/**
 * @brief Initialize WiFi subsystem
 * 
 * @param config Pointer to WiFi initialization configuration
 * @param event_callback Event callback function (can be NULL)
 * @param user_data User data pointer to pass to callback
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_init(const pal_wifi_init_config_t* config, 
                      pal_wifi_event_callback_t event_callback,
                      void* user_data);

/**
 * @brief Deinitialize WiFi subsystem
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_deinit(void);

/**
 * @brief Start WiFi (must call after init)
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_start(void);

/**
 * @brief Stop WiFi
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_stop(void);

// ============================================================================
// Station Mode Functions
// ============================================================================

/**
 * @brief Connect to WiFi access point (Station mode)
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_sta_connect(void);

/**
 * @brief Disconnect from WiFi access point (Station mode)
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_sta_disconnect(void);

/**
 * @brief Check if connected to access point (Station mode)
 * 
 * @return true if connected, false otherwise
 */
bool pal_wifi_sta_is_connected(void);

/**
 * @brief Get RSSI (signal strength) in dBm (Station mode)
 * 
 * @param rssi Pointer to store RSSI value
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_sta_get_rssi(int8_t* rssi);

// ============================================================================
// Access Point Mode Functions
// ============================================================================

/**
 * @brief Get number of stations connected to AP
 * 
 * @param num_sta Pointer to store number of connected stations
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_ap_get_sta_count(uint8_t* num_sta);

// ============================================================================
// Common Information Functions
// ============================================================================

/**
 * @brief Get WiFi MAC address
 * 
 * @param mac Pointer to 6-byte array to store MAC address
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_get_mac(uint8_t mac[6]);

/**
 * @brief Get WiFi MAC address as string
 * 
 * @param mac_str Buffer to store MAC string (min PAL_WIFI_MAC_STR_LEN bytes)
 * @param max_len Maximum length of buffer
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_get_mac_str(char* mac_str, size_t max_len);

/**
 * @brief Get WiFi IP address as string
 * 
 * @param ip_str Buffer to store IP string (min PAL_WIFI_IP_STR_LEN bytes)
 * @param max_len Maximum length of buffer
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_get_ip_str(char* ip_str, size_t max_len);

/**
 * @brief Get comprehensive WiFi status
 * 
 * @param status Pointer to status structure to fill
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_get_status(pal_wifi_status_t* status);

/**
 * @brief Get current WiFi mode
 * 
 * @param mode Pointer to store current mode
 * @return 0 on success, negative error code on failure
 */
int32_t pal_wifi_get_mode(pal_wifi_mode_t* mode);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Calculate WiFi signal level from RSSI
 * 
 * @param rssi Signal strength in dBm
 * @param num_levels Number of levels to divide signal into (e.g., 5 for 0-4)
 * @return Signal level (0 to num_levels-1)
 */
uint8_t pal_wifi_rssi_to_level(int8_t rssi, uint8_t num_levels);

#ifdef __cplusplus
}
#endif

#endif // PAL_WIFI_H
