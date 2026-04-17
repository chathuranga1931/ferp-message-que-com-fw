// hsys_log.h
//
// Framework logging macros — wraps pal_logger directly so that __LINE__
// is always captured at the call site (no function indirection).
//
// Usage (anywhere in the framework or app-messages layer):
//
//   #include "hsys_log.h"
//
//   FWK_LOG_INF("pool init: %u blocks", count);
//   FWK_LOG_ERR("msg create failed for id=0x%04X", msg_id);
//
// All four levels map to the same pal_logger_log() call with the
// appropriate colour/level code.  The tag is always "HSYS_FWK" (8 chars).
//
// No weak functions, no overrides, no hooks — just macros.

#ifndef HSYS_LOG_H
#define HSYS_LOG_H

#include "pal_logger.h"

// "HSYS_FWK" is exactly TAG_SIZE (8) chars — verified at compile time.
#define FWK_TAG "HSYS_FWK"
_Static_assert(sizeof(FWK_TAG) - 1 == TAG_SIZE,
               "FWK_TAG must be exactly TAG_SIZE characters");

/** Debug log — verbose traces. __LINE__ captured at call site. */
#define FWK_LOG_DBG(fmt, ...) \
    pal_logger_log(true, DEBUG_CODE FWK_TAG "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

/** Informational log — lifecycle events, state transitions. */
#define FWK_LOG_INF(fmt, ...) \
    pal_logger_log(true, INFO_CODE  FWK_TAG "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

/** Warning log — unexpected but recoverable conditions. */
#define FWK_LOG_WRN(fmt, ...) \
    pal_logger_log(true, WARN_CODE  FWK_TAG "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

/** Error log — pool exhausted, unknown IDs, init failures. */
#define FWK_LOG_ERR(fmt, ...) \
    pal_logger_log(true, ERROR_CODE FWK_TAG "  %4d : " fmt, __LINE__, ##__VA_ARGS__)

#endif /* HSYS_LOG_H */
