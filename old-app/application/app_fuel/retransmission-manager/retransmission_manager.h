#ifndef RETRANSMISSION_MANAGER_H
#define RETRANSMISSION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "middleware/list-manager/list_manager.h"

// Maximum size of event data
#define RETX_MGR_MAX_EVENT_SIZE 512

// Retransmission event types
typedef enum {
    RETX_EVENT_TYPE_PUMPED = 0,
    RETX_EVENT_TYPE_PRINTED,
    RETX_EVENT_TYPE_UNKNOWN = 0xFF
} retx_event_type_t;

// Retransmission event structure
typedef struct {
    retx_event_type_t type;
    char date_key[16];              // Date when event was created (YYYYMMDD)
    uint16_t payload_len;           // Actual length of payload data
    char payload[RETX_MGR_MAX_EVENT_SIZE];
} retx_event_t;

// Callback for actually sending the event to cloud
// Returns 0 on success, non-zero on failure
typedef int32_t (*retx_send_callback_t)(retx_event_type_t type, const char* payload, size_t payload_len, void* user_data);

// Configuration
typedef struct {
    list_manager_config_t list_config;  // Base list manager config
    retx_send_callback_t send_callback; // Callback to send event to cloud
    void* user_data;                    // User data passed to callback
    uint32_t retry_interval_ms;         // Minimum time between retry attempts
} retx_manager_config_t;

// Runtime handle
typedef struct {
    list_manager_t list_mgr;
    retx_send_callback_t send_callback;
    void* user_data;
    uint32_t retry_interval_ms;
    uint32_t last_retry_time;
    bool is_initialized;
} retx_manager_t;

// --- API ---

// Initialize the retransmission manager
int32_t retx_mgr_init(retx_manager_t *handle, const retx_manager_config_t *config);

// Deinitialize
int32_t retx_mgr_deinit(retx_manager_t *handle);

// Add event that failed to send to cloud
int32_t retx_mgr_add_failed_event(
    retx_manager_t *handle, 
    const char *date_key,
    retx_event_type_t type,
    const char *payload,
    size_t payload_len
);

// Process retransmission (call this periodically)
// Attempts to send one pending event
// Returns: 0 if event sent successfully, -4 if no pending events, other negative on error
int32_t retx_mgr_process(retx_manager_t *handle);

// Get statistics
int32_t retx_mgr_get_pending_count(retx_manager_t *handle, uint32_t *count_out);

// Cleanup old files
int32_t retx_mgr_cleanup(retx_manager_t *handle);

#endif // RETRANSMISSION_MANAGER_H
