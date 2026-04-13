/**
 * @file pal_network.h
 * @brief Platform Abstraction Layer - Network Operations Interface
 * 
 * This header defines a platform-independent interface for network operations
 * including ping, network status checks, and connectivity tests.
 */

#ifndef PAL_NETWORK_H
#define PAL_NETWORK_H

#include "pal_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              STRUCTURES                                   */
/*===========================================================================*/

/**
 * @brief Ping configuration structure
 */
typedef struct {
    const char* target_host;        ///< Target hostname or IP address
    uint32_t count;                 ///< Number of ping requests (0 = single ping)
    uint32_t timeout_ms;            ///< Timeout per ping in milliseconds
    uint32_t interval_ms;           ///< Interval between pings in milliseconds
    size_t data_size;               ///< Size of ping data payload in bytes
} pal_ping_config_t;

/**
 * @brief Ping result structure
 */
typedef struct {
    bool success;                   ///< Overall success status
    uint32_t packets_sent;          ///< Number of packets sent
    uint32_t packets_received;      ///< Number of packets received
    uint32_t packets_lost;          ///< Number of packets lost
    uint32_t min_time_ms;           ///< Minimum round-trip time
    uint32_t max_time_ms;           ///< Maximum round-trip time
    uint32_t avg_time_ms;           ///< Average round-trip time
    uint32_t total_time_ms;         ///< Total time taken
} pal_ping_result_t;

/*===========================================================================*/
/*                         PING OPERATIONS                                   */
/*===========================================================================*/

/**
 * @brief Perform a simple ping to a host
 * 
 * This is a simplified ping function that sends a single ICMP echo request
 * and waits for a response.
 * 
 * @param host Target hostname or IP address (e.g., "8.8.8.8" or "google.com")
 * @param timeout_ms Timeout in milliseconds (0 = use default)
 * @return true if ping successful, false otherwise
 */
bool pal_network_ping(const char* host, uint32_t timeout_ms);

/**
 * @brief Perform a detailed ping with configuration and results
 * 
 * This function provides detailed ping statistics including packet loss,
 * min/max/avg times, etc.
 * 
 * @param config Ping configuration
 * @param result Output: Detailed ping results (can be NULL)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_network_ping_detailed(const pal_ping_config_t* config, pal_ping_result_t* result);

/**
 * @brief Check if network interface is connected
 * 
 * @param is_connected Output: true if connected, false otherwise
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_network_is_connected(bool* is_connected);

/**
 * @brief Get local IP address as string
 * 
 * @param ip_buffer Buffer to store IP address string
 * @param buffer_size Size of buffer
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_network_get_ip_address(char* ip_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // PAL_NETWORK_H
