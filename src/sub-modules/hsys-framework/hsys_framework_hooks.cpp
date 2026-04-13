// hsys_framework_hooks.cpp
//
// Default (weak) implementations of framework log hooks.
//
// These are compiled into hsys_framework and act as a safe fallback that
// writes to stdout via printf.  Any translation unit that provides a
// *strong* definition of hsys_log() / hsys_log_error() — e.g. your
// product-layer hsys_framework_hooks.cpp — will silently override these
// at link time without touching this file.
//
// To override in your product layer, create your own
// hsys_framework_hooks.cpp containing something like:
//
//   #include "hsys_log.h"
//   #include "pal_logger.h"
//
//   // Route framework INFO traces into pal_logger
//   void hsys_log(const char *fmt, ...) {
//       va_list ap;
//       va_start(ap, fmt);
//       pal_logger_logv(true, fmt, ap);
//       va_end(ap);
//   }
//
//   // Route framework ERROR traces into pal_logger
//   void hsys_log_error(const char *fmt, ...) {
//       va_list ap;
//       va_start(ap, fmt);
//       pal_logger_logv(true, fmt, ap);   // or a dedicated error channel
//       va_end(ap);
//   }

#include "hsys_log.h"

#include <stdio.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
// Weak symbols — overridable by the application / product layer
// ---------------------------------------------------------------------------

__attribute__((weak))
void hsys_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    // printf-based fallback does not add a newline — callers include \n in fmt
}

__attribute__((weak))
void hsys_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
