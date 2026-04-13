/**
 * @file pal_esp_idf_network.cpp
 * @brief Platform Abstraction Layer - ESP-IDF Network Implementation
 * 
 * This file implements the network interface for ESP-IDF platform using
 * the esp_ping (ICMP) component and lwIP network stack.
 */

#include "pal_network.h"
#include "pal/pal_logger.h"

#include <string.h>
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

/*===========================================================================*/
/*                            DEFINITIONS                                    */
/*===========================================================================*/

#define __TAG__ "PAL_NETW"

#define NETW_DEBUG_LOG_EN      LOG_DIS
#define NETW_WARN_LOG_EN       LOG_DIS
#define NETW_ERROR_LOG_EN      LOG_DIS
#define NETW_INFO_LOG_EN       LOG_DIS

#define DEFAULT_PING_TIMEOUT_MS 1000
#define DEFAULT_PING_COUNT 1
#define DEFAULT_PING_DATA_SIZE 64
#define DEFAULT_PING_INTERVAL_MS 1000

/*===========================================================================*/
/*                           STATIC VARIABLES                                */
/*===========================================================================*/

static bool ping_success = false;
static pal_ping_result_t* current_result = NULL;

/*===========================================================================*/
/*                          HELPER FUNCTIONS                                 */
/*===========================================================================*/

/**
 * @brief Callback function for ping events
 */
static void on_ping_success(esp_ping_handle_t hdl, void *args) {
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    
    ping_success = true;
    
    if(current_result != NULL) {
        current_result->packets_received++;
        
        // Update timing statistics
        if(current_result->packets_received == 1) {
            current_result->min_time_ms = elapsed_time;
            current_result->max_time_ms = elapsed_time;
        } else {
            if(elapsed_time < current_result->min_time_ms) {
                current_result->min_time_ms = elapsed_time;
            }
            if(elapsed_time > current_result->max_time_ms) {
                current_result->max_time_ms = elapsed_time;
            }
        }
        current_result->total_time_ms += elapsed_time;
    }
    
    LOG_MSG_DEBUG(NETW_DEBUG_LOG_EN, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms",
             recv_len, ipaddr_ntoa(&target_addr), seqno, ttl, elapsed_time);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {
    uint16_t seqno;
    ip_addr_t target_addr;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    
    if(current_result != NULL) {
        current_result->packets_lost++;
    }
    
    LOG_MSG_DEBUG(NETW_DEBUG_LOG_EN, "From %s icmp_seq=%d timeout", ipaddr_ntoa(&target_addr), seqno);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args) {
    uint32_t transmitted, received, total_time_ms;
    
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    
    if(current_result != NULL) {
        current_result->packets_sent = transmitted;
        current_result->packets_received = received;
        current_result->packets_lost = transmitted - received;
        current_result->total_time_ms = total_time_ms;
        
        if(received > 0) {
            current_result->avg_time_ms = current_result->total_time_ms / received;
            current_result->success = true;
        } else {
            current_result->success = false;
        }
    }
    
    LOG_MSG_DEBUG(NETW_DEBUG_LOG_EN, "%d packets transmitted, %d received, time %dms",
             transmitted, received, total_time_ms);
    
    // Delete the ping session here in the callback as per ESP-IDF example
    esp_ping_delete_session(hdl);
}

/*===========================================================================*/
/*                         PING OPERATIONS                                   */
/*===========================================================================*/

bool pal_network_ping(const char* host, uint32_t timeout_ms) {
    if(host == NULL) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Invalid host parameter");
        return false;
    }
    
    // Check if we have an IP address first
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to get network interface");
        return false;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to get IP info");
        return false;
    }
    
    if (ip_info.ip.addr == 0) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "No IP address assigned yet");
        return false;
    }
    
    LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Network ready, IP: " IPSTR, IP2STR(&ip_info.ip));
    
    // Reset ping status
    ping_success = false;
    current_result = NULL;
    
    // Use default timeout if not specified
    if(timeout_ms == 0) {
        timeout_ms = DEFAULT_PING_TIMEOUT_MS;
    }
    
    // Configure ping
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.timeout_ms = timeout_ms;
    ping_config.count = DEFAULT_PING_COUNT;
    ping_config.data_size = DEFAULT_PING_DATA_SIZE;
    ping_config.interval_ms = DEFAULT_PING_INTERVAL_MS;
    
    // Parse IP address - try direct IP first, then DNS resolution
    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    
    struct sockaddr_in6 sock_addr6;
    if (inet_pton(AF_INET6, host, &sock_addr6.sin6_addr) == 1) {
        // IPv6 address
        ipaddr_aton(host, &target_addr);
        LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Target is IPv6: %s", host);
    } else {
        // Try IPv4 or hostname
        struct addrinfo hint;
        struct addrinfo *res = NULL;
        memset(&hint, 0, sizeof(hint));
        
        LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Resolving hostname: %s", host);
        int err = getaddrinfo(host, NULL, &hint, &res);
        if (err != 0 || res == NULL) {
            LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "DNS lookup failed for %s: error %d", host, err);
            return false;
        }
        
        if (res->ai_family == AF_INET) {
            struct in_addr addr4 = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
            inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
            
            char ip_str[16];
            inet_ntoa_r(addr4, ip_str, sizeof(ip_str));
            LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Resolved to IPv4: %s", ip_str);
        } else if (res->ai_family == AF_INET6) {
            struct in6_addr addr6 = ((struct sockaddr_in6 *)(res->ai_addr))->sin6_addr;
            inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
            LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Resolved to IPv6");
        }
        freeaddrinfo(res);
    }
    
    ping_config.target_addr = target_addr;
    
    // Setup callbacks
    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = on_ping_end
    };
    
    // Create ping session
    esp_ping_handle_t ping_handle;
    esp_err_t ret = esp_ping_new_session(&ping_config, &cbs, &ping_handle);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to create ping session: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Start ping
    LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Starting ping...");
    ret = esp_ping_start(ping_handle);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to start ping: %s", esp_err_to_name(ret));
        esp_ping_delete_session(ping_handle);
        return false;
    }
    
    LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Waiting for ping response (timeout: %d ms)...", timeout_ms);
    // Wait for ping to complete (timeout + some margin)
    vTaskDelay(pdMS_TO_TICKS(timeout_ms + 1000));
    
    LOG_MSG_INFO(NETW_DEBUG_LOG_EN, "Ping result: %s", ping_success ? "SUCCESS" : "FAILED");
    
    // Note: Session is deleted in on_ping_end callback, not here
    return ping_success;
}

int32_t pal_network_ping_detailed(const pal_ping_config_t* config, pal_ping_result_t* result) {
    if(config == NULL || config->target_host == NULL) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Invalid configuration");
        return PAL_ERROR_INVALID;
    }
    
    // Initialize result structure
    if(result != NULL) {
        memset(result, 0, sizeof(pal_ping_result_t));
        result->success = false;
        current_result = result;
    } else {
        current_result = NULL;
    }
    
    ping_success = false;
    
    // Configure ping
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.timeout_ms = (config->timeout_ms > 0) ? config->timeout_ms : DEFAULT_PING_TIMEOUT_MS;
    ping_config.count = (config->count > 0) ? config->count : DEFAULT_PING_COUNT;
    ping_config.data_size = (config->data_size > 0) ? config->data_size : DEFAULT_PING_DATA_SIZE;
    ping_config.interval_ms = (config->interval_ms > 0) ? config->interval_ms : DEFAULT_PING_INTERVAL_MS;
    
    // Resolve hostname to IP address
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4
    hints.ai_socktype = SOCK_RAW;
    
    int err = getaddrinfo(config->target_host, NULL, &hints, &res);
    if(err != 0 || res == NULL) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "DNS lookup failed for %s: error %d", config->target_host, err);
        current_result = NULL;
        return PAL_ERROR_NOT_FOUND;
    }
    
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&ping_config.target_addr), &addr->sin_addr);
    freeaddrinfo(res);
    
    // Setup callbacks
    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = on_ping_end
    };
    
    // Create ping session
    esp_ping_handle_t ping_handle;
    esp_err_t ret = esp_ping_new_session(&ping_config, &cbs, &ping_handle);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to create ping session: %s", esp_err_to_name(ret));
        current_result = NULL;
        return PAL_ERROR_INIT;
    }
    
    // Start ping
    ret = esp_ping_start(ping_handle);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(NETW_DEBUG_LOG_EN, "Failed to start ping: %s", esp_err_to_name(ret));
        esp_ping_delete_session(ping_handle);
        current_result = NULL;
        return PAL_ERROR;
    }
    
    // Wait for all pings to complete
    uint32_t total_wait_time = (ping_config.timeout_ms + ping_config.interval_ms) * ping_config.count + 500;
    vTaskDelay(pdMS_TO_TICKS(total_wait_time));
    
    // Stop and cleanup
    esp_ping_stop(ping_handle);
    esp_ping_delete_session(ping_handle);
    
    current_result = NULL;
    
    return PAL_OK;
}

int32_t pal_network_is_connected(bool* is_connected) {
    if(is_connected == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    // Get default network interface (typically WiFi STA or Ethernet)
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(netif == NULL) {
        netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    }
    
    if(netif == NULL) {
        *is_connected = false;
        return PAL_OK;
    }
    
    // Check if interface is up and has an IP address
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    
    if(ret == ESP_OK && ip_info.ip.addr != 0) {
        *is_connected = true;
    } else {
        *is_connected = false;
    }
    
    return PAL_OK;
}

int32_t pal_network_get_ip_address(char* ip_buffer, size_t buffer_size) {
    if(ip_buffer == NULL || buffer_size < 16) {  // Minimum for "255.255.255.255"
        return PAL_ERROR_INVALID;
    }
    
    // Get default network interface
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(netif == NULL) {
        netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    }
    
    if(netif == NULL) {
        strncpy(ip_buffer, "0.0.0.0", buffer_size);
        return PAL_ERROR_NOT_FOUND;
    }
    
    // Get IP info
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    
    if(ret != ESP_OK || ip_info.ip.addr == 0) {
        strncpy(ip_buffer, "0.0.0.0", buffer_size);
        return PAL_ERROR_NOT_FOUND;
    }
    
    // Convert IP to string
    snprintf(ip_buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
    
    return PAL_OK;
}
