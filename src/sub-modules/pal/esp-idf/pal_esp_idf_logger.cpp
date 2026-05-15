/**
 * @file pal_esp_idf_logger.cpp
 * @brief ESP-IDF implementation of logger PAL
 * 
 * This implementation provides logging functionality using UART
 * with timestamp, mutex protection, and various formatting options.
 */

#include "pal_logger.h"
#include "pal_time.h"
#include "hsys_mutex.h"
#include "pal_logger_uart.h"

#include <string.h>
#include <cstdarg>
#include <stdio.h>

// ============================================================================
// Private Variables
// ============================================================================

static hsys_mutex_handle_t s_log_mutex = NULL;
static logger_uart s_log_uart;
static char s_log_buffer[1024];  // Buffer for formatted log messages
static char header[128];

// Sink table
static pal_logger_sink_fn_t s_sinks[PAL_LOGGER_MAX_SINKS] = {};
static uint32_t             s_sink_count = 0;

// Log line counter (wraps at 9999)
static uint32_t log_index = 0;

// Legacy compatibility
char sprintf_buff[100];

#ifdef __cplusplus
// Global logger instance for backward compatibility
logger_class logger;
#endif

void pal_logger_log_header_empty(uint32_t length);

// ============================================================================
// Sink registration
// ============================================================================

int32_t pal_logger_register_sink(pal_logger_sink_fn_t fn)
{
    if (!fn) return -1;
    if (s_log_mutex) hsys_mutex_lock(s_log_mutex);
    int32_t idx = -1;
    for (int i = 0; i < PAL_LOGGER_MAX_SINKS; i++) {
        if (s_sinks[i] == NULL) {
            s_sinks[i] = fn;
            s_sink_count++;
            idx = i;
            break;
        }
    }
    if (s_log_mutex) hsys_mutex_unlock(s_log_mutex);
    return idx;
}

void pal_logger_unregister_sink(int32_t index)
{
    if (index < 0 || index >= PAL_LOGGER_MAX_SINKS) return;
    if (s_log_mutex) hsys_mutex_lock(s_log_mutex);
    if (s_sinks[index] != NULL) {
        s_sinks[index] = NULL;
        if (s_sink_count > 0) s_sink_count--;
    }
    if (s_log_mutex) hsys_mutex_unlock(s_log_mutex);
}

// ============================================================================
// Initialization
// ============================================================================

void pal_logger_init(void) {

    if (s_log_mutex == NULL) {
        s_log_mutex = hsys_mutex_create();
    }

    if(s_log_mutex == NULL) {
        // Handle mutex creation failure (optional: log to UART without mutex)
        s_log_uart.print_str("Critical Error: Failed to create log mutex\r\n");
    }
    else
    {
        s_log_uart.print_str("Logger initialized successfully\r\n");
    }
}

// ============================================================================
// Core Logging Functions
// ============================================================================
#define MAX_MSG_LEN 120  // Max visible characters of user message per line

// Returns the number of visible (non-ANSI-escape) characters in a string
static uint32_t visible_strlen(const char *s) {
    uint32_t count = 0;
    while (*s) {
        if (*s == '\x1b') {
            // Skip ESC [ ... m  (e.g. "\x1b[32m" or "\x1b[0m")
            s++;
            if (*s == '[') {
                s++;
                while (*s && *s != 'm') s++;
                if (*s) s++; // skip 'm'
            }
        } else {
            count++;
            s++;
        }
    }
    return count;
}

static char chunk[MAX_MSG_LEN + 1];

void pal_logger_log(bool en, const char *format, ...) {

    if (s_log_mutex == NULL) {
        return;
    }

    if(!en)
    {
        return;
    }

    if(!hsys_mutex_try_lock(s_log_mutex, 500))
    {
        // Failed to acquire mutex within timeout, log an error and return
        s_log_uart.print_str("Error: Log mutex timeout\r\n");
        return;
    }
    
    va_list args;
    va_start(args, format);
    vsnprintf(s_log_buffer, sizeof(s_log_buffer), format, args);
    va_end(args);

    // Find where the user message starts — after the last " : " separator
    // The format is always: "<ANSI>LVL<ANSI> TAG  NNNN : <user message>"
    const char *msg_start = s_log_buffer;
    const char *sep = strstr(s_log_buffer, " : ");
    if (sep != NULL) {
        msg_start = sep + 3; // skip " : "
    }

    // Visible width of the prefix (level + tag + line + " : "), used for indent
    uint32_t prefix_raw_len = (uint32_t)(msg_start - s_log_buffer); // byte count
    static char prefix_copy[64]; // static: safe because mutex is held for the full call
    if (prefix_raw_len < sizeof(prefix_copy)) {
        strncpy(prefix_copy, s_log_buffer, prefix_raw_len);
        prefix_copy[prefix_raw_len] = '\0';
    } else {
        prefix_copy[0] = '\0';
    }
    uint32_t prefix_visible_len = visible_strlen(prefix_copy);

    snprintf(header, sizeof(header), "%6ld %4ld", (long)pal_time_get_ms(), log_index++);
    if (log_index > 9999) {
        log_index = 0;
    }
    uint32_t hdr_len = strlen(header);
    s_log_uart.print_char_buff(header);

    // Invoke registered sinks
    if (s_sink_count > 0) {
        for (int i = 0; i < PAL_LOGGER_MAX_SINKS; i++) {
            if (s_sinks[i]) {
                s_sinks[i](header, s_log_buffer, strlen(s_log_buffer));
            }
        }
    }

    uint32_t total_indent = hdr_len + prefix_visible_len;

    uint32_t user_msg_raw_len = strlen(msg_start);

    // Print the whole buffer (prefix + first chunk of user message) as first line
    uint32_t first_chunk_len = (user_msg_raw_len < MAX_MSG_LEN) ? user_msg_raw_len : MAX_MSG_LEN;
    // Print prefix
    strncpy(chunk, s_log_buffer, prefix_raw_len < MAX_MSG_LEN ? prefix_raw_len : MAX_MSG_LEN);
    chunk[prefix_raw_len] = '\0';
    s_log_uart.print_str(chunk);
    // Print first user message chunk
    strncpy(chunk, msg_start, first_chunk_len);
    chunk[first_chunk_len] = '\0';
    s_log_uart.print_str(chunk);
    pal_logger_log_footer(true);

    // Print remaining chunks with empty header for alignment
    uint32_t offset = first_chunk_len;
    while (offset < user_msg_raw_len) {
        pal_logger_log_header_empty(total_indent);
        uint32_t chunk_len = (user_msg_raw_len - offset > MAX_MSG_LEN) ? MAX_MSG_LEN : (user_msg_raw_len - offset);
        strncpy(chunk, msg_start + offset, chunk_len);
        chunk[chunk_len] = '\0';
        s_log_uart.print_str(chunk);
        pal_logger_log_footer(true);
        offset += chunk_len;
    }

    // Invoke registered sinks
    if (s_sink_count > 0) {
        for (int i = 0; i < PAL_LOGGER_MAX_SINKS; i++) {
            if (s_sinks[i]) {
                s_sinks[i](" ","\n", 1);
            }
        }
    }
    
    hsys_mutex_unlock(s_log_mutex);   
}

void pal_logger_log_only_serial(bool en, const char *format, ...) {

    if (s_log_mutex == NULL) {
        return;
    }

    if(!en)
    {
        return;
    }

    hsys_mutex_lock(s_log_mutex);
    
    va_list args;
    va_start(args, format);
    vsnprintf(s_log_buffer, sizeof(s_log_buffer), format, args);
    va_end(args);
    
    s_log_uart.print_str(s_log_buffer);    
    
    hsys_mutex_unlock(s_log_mutex);
}

void pal_logger_log_no_newline(bool en, const char *format, ...) {

    if(s_log_mutex == NULL) {
        return;
    }

    if(!en)
    {
        return;
    }

    hsys_mutex_lock(s_log_mutex);
    
    va_list args;
    va_start(args, format);
    vsnprintf(s_log_buffer, sizeof(s_log_buffer), format, args);
    va_end(args);
    
    pal_logger_log_header();
    s_log_uart.print_str(s_log_buffer);    
    
    hsys_mutex_unlock(s_log_mutex);
        
}

uint32_t pal_logger_log_header(void) {
    
    // Use PAL time interface for platform independence
    snprintf(header, sizeof(header), "%6ld %4ld", (long)pal_time_get_ms(), log_index++);
    if (log_index > 9999) {
        log_index = 0;
    }    

    return strlen(header); // visible timestamp width only
}

void pal_logger_log_header_empty(uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        header[i] = ' ';
    }
    header[length] = '\0';  // Null-terminate so print_char_buff stops at the right place
    s_log_uart.print_char_buff(header);
}

void pal_logger_log_footer(bool en) {

    if(!en) return;

    s_log_uart.print_char_buff("\r\n");
}

// ============================================================================
// Buffer Logging Functions
// ============================================================================

void pal_logger_log_buffer_msg(const char *msg, byte *buffer, uint16_t size) {
    if (buffer == NULL) {
        return;
    }
    
    pal_logger_log_header();
    s_log_uart.print_str(msg);
    
    char tmp_buff[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp_buff, sizeof(tmp_buff), "%02X ", buffer[i]);
        s_log_uart.print_str(tmp_buff);
    }
    
    pal_logger_log_footer(true);
}

void pal_logger_log_buffer(bool en, byte *buffer, uint16_t size) {
    
    if (buffer == NULL) {
        return;
    }

    if(!en)
    {
        return;
    }
    
    char tmp_buff[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp_buff, sizeof(tmp_buff), "%02X ", buffer[i]);
        s_log_uart.print_str(tmp_buff);
    }
}

void pal_logger_log_buffer_sep(byte *buffer, uint16_t size, char c) {
    if (buffer == NULL) {
        return;
    }
    
    pal_logger_log_header();
    
    char tmp_buff[7];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp_buff, sizeof(tmp_buff), "%02X%c", buffer[i], c);
        s_log_uart.print_str(tmp_buff);
    }
    
    pal_logger_log_footer(true);
}

void pal_logger_log_uint16_buffer(uint16_t *buffer, uint16_t size) {
    if (buffer == NULL) {
        return;
    }
    
    pal_logger_log_header();
    
    char tmp_buff[10];
    for (uint16_t i = 0; i < size; i++) {
        snprintf(tmp_buff, sizeof(tmp_buff), "%04X ", buffer[i]);
        s_log_uart.print_str(tmp_buff);
    }
    
    pal_logger_log_footer(true);
}

void pal_logger_log_buffer_as_uint16(byte *buffer, uint16_t size) {
    if (buffer == NULL) {
        return;
    }
    
    pal_logger_log_header();
    
    char tmp_buff[10];
    for (uint16_t i = 0; i < size; i += 2) {
        snprintf(tmp_buff, sizeof(tmp_buff), "%02X%02X ", buffer[i + 1], buffer[i]);
        s_log_uart.print_str(tmp_buff);
    }
    
    pal_logger_log_footer(true);
}
