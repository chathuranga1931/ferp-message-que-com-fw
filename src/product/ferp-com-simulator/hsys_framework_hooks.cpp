// hsys_framework_hooks.cpp  — simulator product layer
//
// Strong overrides of the weak hsys_log / hsys_log_error symbols defined in
// the framework.  This file is compiled as part of the simulator product and
// routes all framework log output through pal_logger so that framework traces
// appear with the same timestamp-prefixed format as the rest of the system.
//
// Because these are *strong* definitions they silently replace the weak
// defaults in hsys_framework/hsys_framework_hooks.cpp at link time.

#include "hsys_log.h"
#include "pal_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Tag visible in log output — exactly TAG_SIZE (8) chars
#define __TAG__ "HSYS_FWK"

// pal_logger_log() takes (bool en, const char *fmt, ...) — we need a va_list
// variant.  pal_logger.h does not expose pal_logger_logv(), so we format
// into a local buffer and forward the already-rendered string.
static void forward_to_pal(const char *fmt, va_list ap)
{
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    // Strip the trailing newline that hsys_log callers include — pal_logger
    // adds its own line ending via log_footer.
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    pal_logger_log(true, INFO_CODE __TAG__ "  %4d : %s", 0, buf);
}

void hsys_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    forward_to_pal(fmt, ap);
    va_end(ap);
}

void hsys_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // Reuse the same helper — swap INFO_CODE for ERROR_CODE if you want a
    // distinct colour; here we keep it simple and use the same channel.
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    pal_logger_log(true, ERROR_CODE __TAG__ "  %4d : %s", 0, buf);
}
