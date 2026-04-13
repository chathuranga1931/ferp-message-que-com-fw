// hsys_log.h
//
// Weak-linkable logging hook for the HSYS framework.
//
// By default, hsys_log() and hsys_log_error() are implemented as weak
// symbols that fall back to plain printf().  To redirect framework logs
// into your application's PAL logger, override these two functions by
// providing strong definitions — typically in hsys_framework_hooks.cpp
// placed in your product layer.
//
// Signature is deliberately identical to pal_logger_log() so that a
// one-line hook body suffices:
//
//   void hsys_log(const char *fmt, ...) {
//       va_list ap; va_start(ap, fmt);
//       pal_logger_logv(true, fmt, ap);
//       va_end(ap);
//   }
//
// Log levels used by the framework:
//   hsys_log()        — informational / lifecycle traces
//   hsys_log_error()  — error conditions (pool exhausted, bad IDs, …)

#ifndef HSYS_LOG_H
#define HSYS_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Informational log — lifecycle traces, state changes.
 *         Weak default: printf(fmt, ...) + newline.
 *         Override in hsys_framework_hooks.cpp to route to pal_logger.
 */
void hsys_log(const char *fmt, ...);

/**
 * @brief  Error log — pool exhausted, unknown IDs, init failures.
 *         Weak default: printf(fmt, ...) + newline.
 *         Override in hsys_framework_hooks.cpp to route to pal_logger.
 */
void hsys_log_error(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* HSYS_LOG_H */
