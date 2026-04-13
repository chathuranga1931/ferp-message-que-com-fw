/**
 * @file pal_mac_logger.cpp
 * @brief macOS / Linux implementation of pal_logger.h
 *
 * Mirrors the behaviour of pal_esp_idf_logger.cpp but uses:
 *   - std::mutex  (via hsys_mutex) for thread safety
 *   - pal_time_get_ms() for timestamps (CLOCK_MONOTONIC)
 *   - logger_uart backed by stdout (pal_mac_logger_uart.cpp)
 *
 * Long messages are wrapped at MAX_MSG_LEN visible characters, with
 * subsequent lines padded to align with the first user character, exactly
 * as in the ESP-IDF implementation.
 */

#include "pal_logger.h"
#include "pal_time.h"
#include "pal_logger_uart.h"
#include "hsys_mutex.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// ============================================================================
// Private state
// ============================================================================

static hsys_mutex_handle_t s_log_mutex  = nullptr;
static logger_uart         s_log_uart;
static char                s_log_buffer[1024];

// Legacy compatibility globals required by pal_logger.h
char     sprintf_buff[100];

#ifdef __cplusplus
logger_class logger;
#endif

// Forward declaration (defined below)
static void log_header_empty(uint32_t length);

// ============================================================================
// Helpers
// ============================================================================

#define MAX_MSG_LEN 120

/** Count printable (non-ANSI-escape) characters in a string. */
static uint32_t visible_strlen(const char *s)
{
    uint32_t count = 0;
    while (*s) {
        if (*s == '\x1b') {
            s++;
            if (*s == '[') {
                s++;
                while (*s && *s != 'm') s++;
                if (*s) s++;
            }
        } else {
            count++;
            s++;
        }
    }
    return count;
}

// ============================================================================
// Initialisation
// ============================================================================

void pal_logger_init(void)
{
    if (s_log_mutex == nullptr) {
        s_log_mutex = hsys_mutex_create();
    }
    // Anchor the boot timestamp to program start on first init call.
    (void)pal_time_get_ms();
    // No UART port to open on the simulator — stdout is always available.
}

// ============================================================================
// Header / footer
// ============================================================================

static char    s_header[128];
static uint32_t s_log_index = 0;

uint32_t pal_logger_log_header(void)
{
    snprintf(s_header, sizeof(s_header), "%6llu %4lu",
             (unsigned long long)pal_time_get_ms(),
             (unsigned long)s_log_index++);
    if (s_log_index > 9999) s_log_index = 0;

    s_log_uart.print_char_buff(s_header);
    return (uint32_t)strlen(s_header);
}

static void log_header_empty(uint32_t length)
{
    if (length >= sizeof(s_header)) length = sizeof(s_header) - 1;
    for (uint32_t i = 0; i < length; i++) s_header[i] = ' ';
    s_header[length] = '\0';
    s_log_uart.print_char_buff(s_header);
}

void pal_logger_log_footer(bool en)
{
    if (!en) return;
    s_log_uart.print_char_buff("\n");
}

// ============================================================================
// Core log functions
// ============================================================================

void pal_logger_log(bool en, const char *format, ...)
{
    if (!en || s_log_mutex == nullptr) return;

    hsys_mutex_lock(s_log_mutex);

    va_list args;
    va_start(args, format);
    vsnprintf(s_log_buffer, sizeof(s_log_buffer), format, args);
    va_end(args);

    // Locate user message start — after the last " : " separator inserted by
    // LOG_MSG_* macros:  "<ANSI>LVL<ANSI> TAG  NNNN : <user message>"
    const char *msg_start = s_log_buffer;
    const char *sep       = strstr(s_log_buffer, " : ");
    if (sep) msg_start = sep + 3;

    uint32_t prefix_raw_len = (uint32_t)(msg_start - s_log_buffer);

    char prefix_copy[64] = {};
    if (prefix_raw_len < sizeof(prefix_copy)) {
        strncpy(prefix_copy, s_log_buffer, prefix_raw_len);
    }
    uint32_t prefix_vis_len = visible_strlen(prefix_copy);

    uint32_t hdr_len     = pal_logger_log_header();
    uint32_t total_indent = hdr_len + prefix_vis_len;
    uint32_t msg_raw_len  = (uint32_t)strlen(msg_start);

    // First line: prefix + first chunk
    uint32_t first_len = (msg_raw_len < MAX_MSG_LEN) ? msg_raw_len : MAX_MSG_LEN;

    char chunk[MAX_MSG_LEN + 1];
    // print prefix
    uint32_t plen = (prefix_raw_len < MAX_MSG_LEN) ? prefix_raw_len : MAX_MSG_LEN;
    strncpy(chunk, s_log_buffer, plen);
    chunk[plen] = '\0';
    s_log_uart.print_str(chunk);
    // print first user-message chunk
    strncpy(chunk, msg_start, first_len);
    chunk[first_len] = '\0';
    s_log_uart.print_str(chunk);
    pal_logger_log_footer(true);

    // Remaining chunks with blank header indent
    uint32_t offset = first_len;
    while (offset < msg_raw_len) {
        log_header_empty(total_indent);
        uint32_t chunk_len = ((msg_raw_len - offset) > MAX_MSG_LEN)
                             ? MAX_MSG_LEN
                             : (msg_raw_len - offset);
        strncpy(chunk, msg_start + offset, chunk_len);
        chunk[chunk_len] = '\0';
        s_log_uart.print_str(chunk);
        pal_logger_log_footer(true);
        offset += chunk_len;
    }

    hsys_mutex_unlock(s_log_mutex);
}

void pal_logger_log_no_newline(bool en, const char *format, ...)
{
    if (!en || s_log_mutex == nullptr) return;

    hsys_mutex_lock(s_log_mutex);

    va_list args;
    va_start(args, format);
    vsnprintf(s_log_buffer, sizeof(s_log_buffer), format, args);
    va_end(args);

    pal_logger_log_header();
    s_log_uart.print_str(s_log_buffer);

    hsys_mutex_unlock(s_log_mutex);
}

// ============================================================================
// Buffer logging
// ============================================================================

void pal_logger_log_buffer(bool en, byte *buffer, uint16_t size)
{
    if (!en || !buffer) return;
    char tmp[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", buffer[i]);
        s_log_uart.print_str(tmp);
    }
}

void pal_logger_log_buffer_sep(byte *buffer, uint16_t size, char c)
{
    if (!buffer) return;
    pal_logger_log_header();
    char tmp[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp, sizeof(tmp), "%02X%c", buffer[i], c);
        s_log_uart.print_str(tmp);
    }
    pal_logger_log_footer(true);
}

void pal_logger_log_buffer_msg(const char *msg, byte *buffer, uint16_t size)
{
    if (!buffer) return;
    pal_logger_log_header();
    s_log_uart.print_str(msg);
    char tmp[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", buffer[i]);
        s_log_uart.print_str(tmp);
    }
    pal_logger_log_footer(true);
}

void pal_logger_log_uint16_buffer(uint16_t *buffer, uint16_t size)
{
    if (!buffer) return;
    pal_logger_log_header();
    char tmp[10];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp, sizeof(tmp), "%04X ", buffer[i]);
        s_log_uart.print_str(tmp);
    }
    pal_logger_log_footer(true);
}

void pal_logger_log_buffer_as_uint16(byte *buffer, uint16_t size)
{
    if (!buffer) return;
    pal_logger_log_header();
    char tmp[10];
    for (uint16_t i = 0; i < size; i += 2) {
        snprintf(tmp, sizeof(tmp), "%02X%02X ", buffer[i + 1], buffer[i]);
        s_log_uart.print_str(tmp);
    }
    pal_logger_log_footer(true);
}
