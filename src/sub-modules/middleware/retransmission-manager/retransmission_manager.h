// retransmission_manager.h
//
// Middleware: circular day-based queue for failed cloud events.
//
// Uses list_manager internally for per-day file rotation (7 days).
// The caller provides a send_callback; retx_mgr_process() calls it
// for the oldest pending event and acks on success.
//
// Usage:
//   retx_manager_config_t cfg = { .list_config = {...}, .send_callback = my_fn };
//   retx_mgr_init(&mgr, &cfg);
//   retx_mgr_add_failed_event(&mgr, "20260505", RETX_EVENT_TYPE_PUMPED, json, len);
//   retx_mgr_process(&mgr);  // call periodically

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "list_manager.h"

// ---------------------------------------------------------------------------
// Max serialized payload per event
// ---------------------------------------------------------------------------

#define RETX_MGR_MAX_EVENT_SIZE   512

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------

typedef enum {
    RETX_EVENT_TYPE_PUMPED  = 0,
    RETX_EVENT_TYPE_PRINTED = 1,
    RETX_EVENT_TYPE_UNKNOWN = 0xFF,
} retx_event_type_t;

// ---------------------------------------------------------------------------
// Internal event record (serialized into list_manager lines)
// ---------------------------------------------------------------------------

typedef struct {
    retx_event_type_t type;
    char  date_key[16];
    uint16_t payload_len;
    char  payload[RETX_MGR_MAX_EVENT_SIZE];
} retx_event_t;

// ---------------------------------------------------------------------------
// Send callback
// Returns 0 on success, non-zero on failure (event stays in queue).
// ---------------------------------------------------------------------------

typedef int32_t (*retx_send_callback_t)(retx_event_type_t type,
                                        const char       *payload,
                                        size_t            payload_len,
                                        void             *user_data);

// ---------------------------------------------------------------------------
// Config (pass once to retx_mgr_init)
// ---------------------------------------------------------------------------

typedef struct {
    list_manager_config_t list_config;      ///< Storage path, prefix, lines/file, days
    retx_send_callback_t  send_callback;    ///< Function that does the actual cloud send
    void                 *user_data;        ///< Passed as-is to send_callback
    uint32_t              retry_interval_ms;///< Minimum ms between retransmission attempts
} retx_manager_config_t;

// ---------------------------------------------------------------------------
// Runtime handle — embed in owning module
// ---------------------------------------------------------------------------

typedef struct {
    list_manager_t        list_mgr;
    retx_send_callback_t  send_callback;
    void                 *user_data;
    uint32_t              retry_interval_ms;
    uint32_t              last_retry_time_ms;
    bool                  is_initialized;
} retx_manager_t;

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

#define RETX_MGR_OK                  0
#define RETX_MGR_ERR_NULL_PARAM     -1
#define RETX_MGR_ERR_NOT_INIT       -2
#define RETX_MGR_ERR_LIST_FAILED    -3
#define RETX_MGR_ERR_NO_DATA        -4
#define RETX_MGR_ERR_SEND_FAILED    -5
#define RETX_MGR_ERR_TOO_SOON       -6

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the retransmission manager.
 *         Calls list_mgr_init() internally.
 */
int32_t retx_mgr_init(retx_manager_t *handle, const retx_manager_config_t *config);

/**
 * @brief  Flush metadata and deinitialise.
 */
int32_t retx_mgr_deinit(retx_manager_t *handle);

/**
 * @brief  Persist a failed event.
 * @param  date_key  "YYYYMMDD" string for the current day.
 * @param  type      Event category.
 * @param  payload   Serialised event data (e.g. JSON string).
 * @param  payload_len  Byte length of payload (max RETX_MGR_MAX_EVENT_SIZE).
 */
int32_t retx_mgr_add_failed_event(retx_manager_t   *handle,
                                   const char       *date_key,
                                   retx_event_type_t type,
                                   const char       *payload,
                                   size_t            payload_len);

/**
 * @brief  Attempt to send one pending event via send_callback.
 *         Returns RETX_MGR_ERR_NO_DATA when the queue is empty.
 *         Returns RETX_MGR_ERR_TOO_SOON if called before retry_interval_ms.
 *         On success, the event is acknowledged (removed from queue).
 *         On send failure, the event remains for the next call.
 */
int32_t retx_mgr_process(retx_manager_t *handle);

/**
 * @brief  Returns the total number of pending (un-acked) events across all days.
 */
int32_t retx_mgr_get_pending_count(retx_manager_t *handle, uint32_t *count_out);

/**
 * @brief  Delete files older than max_tracked_days.
 *         Safe to call at startup or during idle time.
 */
int32_t retx_mgr_cleanup(retx_manager_t *handle);

#ifdef __cplusplus
}
#endif
