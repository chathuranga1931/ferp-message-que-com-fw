/**
 * @file pal_udp_log.h
 * @brief Platform Abstraction Layer — UDP log driver interface.
 *
 * Single shared header for all platforms.  Platform-specific implementations:
 *   ESP-IDF  →  src/sub-modules/pal/esp-idf/pal_esp_idf_udp_log.cpp
 *   macOS/PC →  src/sub-modules/pal/mac-pc/pal_mac_udp_log.cpp  (no-op stubs)
 *
 * Data flow (ESP-IDF):
 *   pal_logger_log()
 *     → pal_udp_log_sink()       registered pal_logger sink callback
 *         strips ANSI, chunks into ≤256-byte entries, enqueues each,
 *         then calls the registered wake callback.
 *     → ModuleUdpLog::on_wake()
 *         → pal_udp_log_drain()  sends all queued entries via UDP
 *
 * Typical usage sequence:
 *   1. pal_udp_log_init()                    — on MsgConfigReady
 *   2. pal_udp_log_start()                   — on MsgWifiEvent(GOT_IP)
 *   3. pal_logger_register_sink(pal_udp_log_sink)
 *   4. pal_logger_unregister_sink(idx)        — on MsgWifiEvent(DISCONNECTED)
 *      pal_udp_log_stop()
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wake callback type.
 *
 * Invoked from within pal_udp_log_sink() — which runs on the logging
 * caller's thread while holding the log mutex — to notify the consumer
 * module that data is ready to drain.  Must be ISR / mutex-safe (typically
 * just calls HsysModule::wake()).
 */
typedef void (*pal_udp_log_wake_fn_t)(void);

/**
 * @brief Initialise (or re-initialise) the UDP log driver.
 *
 * Safe to call multiple times (e.g. after a config change or reconnect).
 * Creates / recreates the internal queue.  Does NOT open a socket — call
 * pal_udp_log_start() for that.
 *
 * @param server_ip    Dot-decimal IPv4 address of the log server.
 * @param port         Destination UDP port.
 * @param mac_no_colon Device MAC without colons, e.g. "AABBCCDDEEFF".
 *                     Prepended to every packet.  Pass NULL to omit.
 * @param wake_fn      Called when data is enqueued; may be NULL.
 */
void pal_udp_log_init(const char            *server_ip,
                      uint16_t               port,
                      const char            *mac_no_colon,
                      pal_udp_log_wake_fn_t  wake_fn);

/** @brief Open the UDP socket.  Call after WiFi gets an IP address. */
void pal_udp_log_start(void);

/** @brief Close the UDP socket.  Call on WiFi disconnect. */
void pal_udp_log_stop(void);

/** @return true if the socket is open and ready to send. */
bool pal_udp_log_is_running(void);

/**
 * @brief pal_logger sink callback — compatible with pal_logger_sink_fn_t.
 *
 * Strips ANSI escape codes, splits the result into ≤256-byte chunks, and
 * enqueues each chunk (silent drop on overflow).  Calls the wake callback
 * once per pal_logger_log() invocation (not per chunk).
 *
 * Register with:  pal_logger_register_sink(pal_udp_log_sink)
 */
void pal_udp_log_sink(const char * header, const char *buf, size_t len);

/**
 * @brief Drain the queue and send all pending entries via UDP.
 *
 * Called by ModuleUdpLog::on_wake().  Uses timeout 0 (never blocks).
 * No-op when the socket is not open.
 */
void pal_udp_log_drain(void);

#ifdef __cplusplus
}
#endif
