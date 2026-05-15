/**
 * @file pal_esp_idf_udp_log.cpp
 * @brief ESP-IDF implementation of the UDP log PAL (see pal_udp_log.h).
 */

#include "pal_udp_log.h"
#include "pal_logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <string.h>
#include <stdio.h>

#define __TAG__  "PAL_UDP "

// ============================================================================
// Configuration
// ============================================================================

#define UDP_QUEUE_DEPTH   20    ///< Maximum outstanding log chunks
#define UDP_ENTRY_SIZE    256   ///< Bytes per queue entry (including null)
#define UDP_PREFIX_MAX    32    ///< "AABBCCDDEEFF " + null

// ============================================================================
// Types
// ============================================================================

typedef struct {
    char data[UDP_ENTRY_SIZE];
} udp_log_entry_t;

// ============================================================================
// Private state
// ============================================================================

static QueueHandle_t          s_queue        = NULL;
static int                    s_sock         = -1;
static struct sockaddr_in     s_dest         = {};
static char                   s_prefix[UDP_PREFIX_MAX] = {};
static pal_udp_log_wake_fn_t  s_wake_fn      = NULL;
static bool                   s_initialized  = false;
static bool                   s_running      = false;

// ============================================================================
// Helpers
// ============================================================================

/**
 * Strip ANSI escape codes from src into dst.
 * @return number of bytes written to dst (not counting null terminator).
 */
static size_t strip_ansi(const char *src, size_t src_len,
                          char       *dst, size_t dst_size)
{
    size_t out = 0;
    size_t i   = 0;
    while (i < src_len && out < dst_size - 1) {
        if (src[i] == '\x1b' && (i + 1) < src_len && src[i + 1] == '[') {
            i += 2;  // skip ESC [
            while (i < src_len && src[i] != 'm') i++;
            if (i < src_len) i++;  // skip 'm'
        } else {
            dst[out++] = src[i++];
        }
    }
    dst[out] = '\0';
    return out;
}

// ============================================================================
// Public API
// ============================================================================

void pal_udp_log_init(const char             *server_ip,
                      uint16_t                port,
                      const char             *mac_no_colon,
                      pal_udp_log_wake_fn_t   wake_fn)
{
    // Re-create queue (safe to call multiple times)
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    s_queue = xQueueCreate(UDP_QUEUE_DEPTH, sizeof(udp_log_entry_t));

    // Configure destination address
    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    s_dest.sin_port   = htons(port);
    inet_pton(AF_INET, server_ip ? server_ip : "0.0.0.0", &s_dest.sin_addr);

    // Build prefix string: "AABBCCDDEEFF : " (matches old app_com.cpp format)
    // if (mac_no_colon && mac_no_colon[0] != '\0') {
    //     snprintf(s_prefix, sizeof(s_prefix), "%s : ", mac_no_colon);
    // } else {
    //     s_prefix[0] = '\0';
    // }
    snprintf(s_prefix, sizeof(s_prefix), "112233445566 : ");
    
    s_wake_fn    = wake_fn;
    s_initialized = true;
    s_running     = false;
}

void pal_udp_log_start(void)
{
    if (!s_initialized || s_queue == NULL) return;

    // Close previous socket if any
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock >= 0) {
        // Non-blocking send: give up after 2 s rather than blocking forever
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        s_running = true;
    }
}

void pal_udp_log_stop(void)
{
    s_running = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}

bool pal_udp_log_is_running(void)
{
    return s_running;
}

void pal_udp_log_sink(const char *header, const char *buf, size_t len)
{
    if (!s_initialized || s_queue == NULL || len == 0) return;

    // Strip ANSI into a local scratch buffer
    // Max stripped size equals len (stripping can only shrink the string)
    static char stripped[UDP_ENTRY_SIZE * 2];

    

    size_t stripped_len = strip_ansi(header, strlen(header), stripped, sizeof(stripped));
    if (stripped_len == 0) return;

    stripped_len += strip_ansi(buf, len, stripped + stripped_len, sizeof(stripped) - stripped_len);
    if (stripped_len == 0) return;

    // Chunk and enqueue (silent drop on overflow)
    size_t offset = 0;
    bool   queued = false;
    while (offset < stripped_len) {
        udp_log_entry_t entry = {};
        size_t chunk = stripped_len - offset;
        if (chunk > UDP_ENTRY_SIZE - 1) chunk = UDP_ENTRY_SIZE - 1;
        memcpy(entry.data, stripped + offset, chunk);
        entry.data[chunk] = '\0';
        offset += chunk;

        // Use timeout=0 so we never block while holding the log mutex
        if (xQueueSend(s_queue, &entry, 0) == pdTRUE) {
            queued = true;
        }
    }

    // Wake consumer once, not once per chunk
    if (queued && s_wake_fn) {
        s_wake_fn();
    }
}

void pal_udp_log_drain(void)
{
    if (!s_running || s_sock < 0 || s_queue == NULL) return;
    // LOG_MSG_INFO_ONLY_SERIAL(true, "udp_drain");

    static char packet[UDP_PREFIX_MAX + UDP_ENTRY_SIZE];
    udp_log_entry_t entry;

    while (xQueueReceive(s_queue, &entry, 0) == pdTRUE) {
        int plen = snprintf(packet, sizeof(packet), "%s%s", s_prefix, entry.data);
        if (plen > 0) {
            sendto(s_sock, packet, (size_t)plen, 0,
                   (const struct sockaddr *)&s_dest, sizeof(s_dest));
            // LOG_MSG_INFO_ONLY_SERIAL(true, "udp_sent");
        }
    }
}
