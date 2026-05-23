/**
 * @file pal_logger.h
 * @brief Platform Abstraction Layer for logging operations
 * 
 * This header provides a platform-independent interface for logging
 * with support for different log levels, buffer logging, and formatted output.
 */

#ifndef PAL_LOGGER_H
#define PAL_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// ============================================================================
// Type Definitions
// ============================================================================

typedef unsigned char byte;

// ============================================================================
// Configuration Macros
// ============================================================================

#define TAG_SIZE 8  // Define the desired size of the tag (max 8)
#define STRING_LENGTH(str) ((sizeof(str) - 1))
#define CHECK_TAG_SIZE(tag) \
    _Static_assert(STRING_LENGTH(tag) == TAG_SIZE, "Error: tag must be exactly 8 characters!")

// ANSI Color Codes for Terminal Output
#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"

#define ERROR_CODE   ANSI_RED    " ERR" ANSI_RESET " "
#define DEBUG_CODE   ANSI_GREEN  " DEB" ANSI_RESET " "
#define WARN_CODE    ANSI_YELLOW " WAR" ANSI_RESET " "
#define INFO_CODE    ANSI_BLUE   " INF" ANSI_RESET " "

// ============================================================================
// Logging Macros
// ============================================================================

#define LOG_EN      true  // Set to false to disable logging at compile time
#define LOG_DIS     false

#define LOG_MSG_INFO_ONLY_SERIAL(en, fmt, ...) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log_only_serial(en, INFO_CODE __TAG__ "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#define LOG_MSG_INFO(en, fmt, ...) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log(en, INFO_CODE __TAG__ "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#define LOG_MSG_DEBUG(en, fmt, ...) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log(en, DEBUG_CODE __TAG__ "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#define LOG_MSG_ERROR(en, fmt, ...) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log(en, ERROR_CODE __TAG__ "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#define LOG_MSG_WARNING(en, fmt, ...) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log(en, WARN_CODE __TAG__ "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#define LOG_DEBUG_BUFFER(en, msg, buf, len) \
    CHECK_TAG_SIZE(__TAG__); \
    pal_logger_log_no_newline(en, DEBUG_CODE __TAG__ "  %4d : " "%s", __LINE__, msg); \
    pal_logger_log_buffer(en, (byte *)buf, len); \
    pal_logger_log_footer(en)

/**
 * @brief Error checking macro for return codes
 * 
 * Checks if the return code indicates an error. If so, logs the error
 * and returns the error code. Similar to ESP_ERROR_CHECK but uses PAL logging.
 * 
 * @param x Expression that returns an error code (0 = success, non-zero = error)
 */
#define PAL_ERROR_CHECK(x) do { \
    int32_t __err_rc = (x); \
    if (__err_rc != 0) { \
        LOG_MSG_ERROR(LOG_EN, "Error check failed: %s returned 0x%x at %s:%d", \
                      #x, __err_rc, __FILE__, __LINE__); \
        return __err_rc; \
    } \
} while(0)

// ============================================================================
// Sink callback table
// ============================================================================

/**
 * @brief Sink callback type.
 *
 * Called after every complete log line (header + content + footer) is written
 * to UART.  The buffer contains the full raw bytes that were sent — including
 * any ANSI escape codes — followed by the line terminator.
 *
 * @param buf  Pointer to the line buffer.
 * @param len  Length of the line in bytes (not including the null terminator).
 */
typedef void (*pal_logger_sink_fn_t)(const char *buf1, const char *buf2, size_t len);

/** Maximum number of simultaneously registered sinks. */
#define PAL_LOGGER_MAX_SINKS  4

/**
 * @brief Register a sink callback.
 *
 * Finds the first free slot in the sink table, stores the callback, and
 * increments the active-sink count.
 *
 * @param fn  Non-NULL callback to register.
 * @return    Slot index (0 .. PAL_LOGGER_MAX_SINKS-1) on success, -1 if the
 *            table is full or @p fn is NULL.
 */
int32_t pal_logger_register_sink(pal_logger_sink_fn_t fn);

/**
 * @brief Unregister a previously registered sink.
 *
 * Clears the slot at @p index and decrements the active-sink count.  Safe to
 * call with an invalid index (no-op).
 *
 * @param index  The value returned by pal_logger_register_sink().
 */
void pal_logger_unregister_sink(int32_t index);

// ============================================================================
// Function Declarations
// ============================================================================

/**
 * @brief Initialize the logger
 * 
 * Must be called before any logging operations.
 * Initializes internal resources (mutexes, buffers, etc.)
 */
void pal_logger_init(void);

/**
 * @brief Log a formatted message with newline
 * 
 * Logs a formatted string with timestamp, line number, and newline.
 * Thread-safe implementation with mutex protection.
 * 
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
void pal_logger_log(bool en, const char *format, ...);

/**
 * @brief Log a formatted message (va_list variant — no intermediate buffer)
 *
 * Same as pal_logger_log() but accepts an already-opened va_list.  Callers
 * with their own variadic argument list (e.g. wrapper functions) should use
 * this instead of pre-formatting into a stack buffer and calling
 * pal_logger_log("%s", buf), which would waste up to 1 KB of stack and run
 * vsnprintf twice.
 */
void pal_logger_vlog(bool en, const char *format, va_list args);

/**
 * @brief Log with a module-name prefix (zero stack cost for message content).
 *
 * Formats "[module_name] user_message" directly into the already-static
 * internal log buffer under the log mutex.  The message content never
 * touches the caller's stack — only the va_list pointer is passed.
 *
 * @param en         Enable flag (false = no-op).
 * @param is_error   true → ERROR level colour, false → INFO level colour.
 * @param prefix     Module name or other short prefix string.
 * @param format     Printf-style user format string.
 * @param args       Opened va_list for the user format arguments.
 */
void pal_logger_vlog_prefix(bool en, bool is_error, const char *prefix,
                             const char *format, va_list args);

void pal_logger_log_only_serial(bool en, const char *format, ...);
/**
 * @brief Log a formatted message without newline
 * 
 * Logs a formatted string with timestamp and line number, but no newline.
 * Useful for continuing log messages or buffer dumps.
 * 
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
void pal_logger_log_no_newline(bool en, const char *format, ...);

/**
 * @brief Log a byte buffer in hexadecimal format
 * 
 * Logs buffer contents in hex format: "XX XX XX ..."
 * Does not add header or footer.
 * 
 * @param buffer Pointer to byte buffer
 * @param size Size of buffer in bytes
 */
void pal_logger_log_buffer(bool en, byte *buffer, uint16_t size);

/**
 * @brief Log a byte buffer with custom separator
 * 
 * Logs buffer contents in hex format with custom separator character.
 * Includes header and footer.
 * 
 * @param buffer Pointer to byte buffer
 * @param size Size of buffer in bytes
 * @param c Separator character (e.g., ' ', ',', '\n')
 */
void pal_logger_log_buffer_sep(byte *buffer, uint16_t size, char c);

/**
 * @brief Log a byte buffer with message prefix
 * 
 * Logs a message string followed by buffer contents in hex format.
 * Includes header and footer.
 * 
 * @param msg Prefix message string
 * @param buffer Pointer to byte buffer
 * @param size Size of buffer in bytes
 */
void pal_logger_log_buffer_msg(const char *msg, byte *buffer, uint16_t size);

/**
 * @brief Log a uint16_t buffer in hexadecimal format
 * 
 * Logs uint16_t array contents in hex format: "XXXX XXXX ..."
 * Includes header and footer.
 * 
 * @param buffer Pointer to uint16_t buffer
 * @param size Number of uint16_t elements
 */
void pal_logger_log_uint16_buffer(uint16_t *buffer, uint16_t size);

/**
 * @brief Log a byte buffer as uint16_t values
 * 
 * Interprets byte buffer as little-endian uint16_t values and logs in hex format.
 * Includes header and footer.
 * 
 * @param buffer Pointer to byte buffer (must be even size)
 * @param size Size of buffer in bytes (should be even)
 */
void pal_logger_log_buffer_as_uint16(byte *buffer, uint16_t size);

/**
 * @brief Log header with timestamp and log index
 * 
 * Internal function to add timestamp and sequence number prefix.
 * Format: "timestamp logIndex"
 */
uint32_t pal_logger_log_header(void);

/**
 * @brief Log footer (newline)
 * 
 * Internal function to add newline footer.
 */
void pal_logger_log_footer(bool en);

// ============================================================================
// Legacy Compatibility (for easier migration)
// ============================================================================

#ifdef __cplusplus

#include <cstdio>

/**
 * @brief Legacy logger class wrapper for C++ code
 * 
 * This class wraps PAL logger functions to provide backward compatibility
 * with existing code that uses the logger object.
 */
class logger_class {
public:
    void init(void) {
        pal_logger_init();
    }
    
    void log(const char *format, ...) {
        va_list args;
        va_start(args, format);
        pal_logger_vlog(true, format, args);
        va_end(args);
    }
    
    void log_no_newline(const char *format, ...) {
        va_list args;
        va_start(args, format);
        pal_logger_vlog(true, format, args);
        va_end(args);
    }
    
    void log_buffer(byte *buffer, uint16_t size) {
        pal_logger_log_buffer(true, buffer, size);
    }
    
    void log_buffer_(byte *buffer, uint16_t size, char c) {
        pal_logger_log_buffer(true, buffer, size);
    }
    
    void log_buffer(byte *buffer, uint16_t size, char c) {
        pal_logger_log_buffer_sep(buffer, size, c);
    }
    
    void log_buffer(const char *msg, byte *buffer, uint16_t size) {
        pal_logger_log_buffer_msg(msg, buffer, size);
    }
    
    void log_uint16_buffer(uint16_t *buffer, uint16_t size) {
        pal_logger_log_uint16_buffer(buffer, size);
    }
    
    void log_buffer_as_uint16(byte *buffer, uint16_t size) {
        pal_logger_log_buffer_as_uint16(buffer, size);
    }
    
    void log_header(void) {
        pal_logger_log_header();
    }
    
    void log_footer(void) {
        pal_logger_log_footer(true);
    }
};

// Global logger instance for backward compatibility
extern logger_class logger;

#endif /* __cplusplus */

// Provide compatibility defines for existing code during migration
#define logger_class_init() pal_logger_init()

// Global sprintf buffer (legacy compatibility)
extern char sprintf_buff[100];

#ifdef __cplusplus
}
#endif

#endif /* PAL_LOGGER_H */
