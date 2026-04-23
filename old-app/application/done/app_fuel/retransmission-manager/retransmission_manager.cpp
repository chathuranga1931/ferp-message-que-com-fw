#include "retransmission_manager.h"
#include <string.h>
#include <stdio.h>

#include "pal_logger.h"


#define __TAG__  "APP_RETX"

#define RETX_LOG_EN LOG_DIS

#define RETX_LOG_DEBUG(fmt, ...) LOG_MSG_DEBUG(RETX_LOG_EN, fmt, ##__VA_ARGS__)
#define RETX_LOG_ERROR(fmt, ...) LOG_MSG_ERROR(RETX_LOG_EN, fmt, ##__VA_ARGS__)

// Error codes
#define RETX_MGR_OK                     0
#define RETX_MGR_ERR_NULL_PARAM         -1
#define RETX_MGR_ERR_NOT_INITIALIZED    -2
#define RETX_MGR_ERR_LIST_FAILED        -3
#define RETX_MGR_ERR_NO_DATA            -4
#define RETX_MGR_ERR_SEND_FAILED        -5
#define RETX_MGR_ERR_TOO_SOON           -6

// Helper: Serialize event to string
static void _serialize_event(const retx_event_t *event, char *out_buffer, size_t max_len) {
    // Format: TYPE|LENGTH|PAYLOAD
    RETX_LOG_DEBUG("Serializing event: type=%d, payload_len=%u", (int)event->type, event->payload_len);
    
    // Write header
    int header_len = snprintf(out_buffer, max_len, "%d|%u|", (int)event->type, event->payload_len);
    
    // Copy payload bytes (may contain null bytes)
    if (header_len > 0 && (header_len + event->payload_len) < max_len) {
        memcpy(out_buffer + header_len, event->payload, event->payload_len);
        RETX_LOG_DEBUG("Serialization complete: total_size=%d", header_len + event->payload_len);
    } else {
        RETX_LOG_ERROR("Serialization failed: buffer too small");
    }
}

// Helper: Deserialize event from string
static int32_t _deserialize_event(const char *buffer, retx_event_t *event) {
    int type_int;
    unsigned int payload_len;
    
    RETX_LOG_DEBUG("Deserializing event from buffer");
    
    // Parse header: TYPE|LENGTH|
    int items = sscanf(buffer, "%d|%u|", &type_int, &payload_len);
    
    if (items != 2) {
        RETX_LOG_ERROR("Deserialization failed: invalid header format (items=%d)", items);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    if (payload_len > RETX_MGR_MAX_EVENT_SIZE) {
        RETX_LOG_ERROR("Deserialization failed: payload too large (%u > %d)", 
            payload_len, RETX_MGR_MAX_EVENT_SIZE);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    event->type = (retx_event_type_t)type_int;
    event->payload_len = (uint16_t)payload_len;
    
    RETX_LOG_DEBUG("Parsed header: type=%d, payload_len=%u", type_int, payload_len);
    
    // Find payload start (after second '|')
    const char *payload_start = buffer;
    int pipes_found = 0;
    while (*payload_start && pipes_found < 2) {
        if (*payload_start == '|') {
            pipes_found++;
        }
        payload_start++;
    }
    
    // Copy payload
    if (pipes_found == 2) {
        memcpy(event->payload, payload_start, event->payload_len);
        RETX_LOG_DEBUG("Deserialization successful");
    } else {
        RETX_LOG_ERROR("Deserialization failed: payload delimiter not found");
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    return RETX_MGR_OK;
}

// Helper: Get current time in milliseconds (needs to be implemented based on your platform)
static uint32_t _get_current_time_ms(void) {
    // TODO: Replace with actual platform time function
    // For ESP32: return millis();
    // For now, return 0 (will be replaced by user)
    extern uint32_t board_millis(void);
    return board_millis();
}

// === Public API Implementation ===

int32_t retx_mgr_init(retx_manager_t *handle, const retx_manager_config_t *config) {
    if (!handle || !config || !config->send_callback) {
        RETX_LOG_ERROR("Init failed: NULL parameter");
        return RETX_MGR_ERR_NULL_PARAM;
    }
    
    RETX_LOG_DEBUG("Initializing retransmission manager:");
    RETX_LOG_DEBUG("  retry_interval_ms: %u", config->retry_interval_ms);
    
    memset(handle, 0, sizeof(retx_manager_t));
    
    // Initialize the underlying list manager
    int32_t ret = list_mgr_init(&handle->list_mgr, &config->list_config);
    if (ret != 0) {
        RETX_LOG_ERROR("Failed to initialize list manager, ret=%d", ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    handle->send_callback = config->send_callback;
    handle->user_data = config->user_data;
    handle->retry_interval_ms = config->retry_interval_ms;
    handle->last_retry_time = 0;
    handle->is_initialized = true;
    
    RETX_LOG_DEBUG("Retransmission manager initialized successfully");
    return RETX_MGR_OK;
}

int32_t retx_mgr_deinit(retx_manager_t *handle) {
    if (!handle || !handle->is_initialized) {
        RETX_LOG_ERROR("Deinit failed: not initialized");
        return RETX_MGR_ERR_NOT_INITIALIZED;
    }
    
    RETX_LOG_DEBUG("Deinitializing retransmission manager");
    
    list_mgr_deinit(&handle->list_mgr);
    handle->is_initialized = false;
    
    RETX_LOG_DEBUG("Retransmission manager deinitialized");
    return RETX_MGR_OK;
}

int32_t retx_mgr_add_failed_event(
    retx_manager_t *handle, 
    const char *date_key,
    retx_event_type_t type,
    const char *payload,
    size_t payload_len
) {
    if (!handle || !handle->is_initialized || !date_key || !payload) {
        RETX_LOG_ERROR("Add failed event: NULL parameter");
        return RETX_MGR_ERR_NULL_PARAM;
    }
    
    if (payload_len > RETX_MGR_MAX_EVENT_SIZE) {
        RETX_LOG_ERROR("Add failed event: payload too large (%u > %d)", 
            payload_len, RETX_MGR_MAX_EVENT_SIZE);
        return RETX_MGR_ERR_NULL_PARAM;
    }
    
    RETX_LOG_DEBUG("Adding failed event: date=%s, type=%d, payload_len=%u", 
        date_key, (int)type, payload_len);
    
    // Create event structure
    retx_event_t event;
    event.type = type;
    event.payload_len = (uint16_t)payload_len;
    strncpy(event.date_key, date_key, sizeof(event.date_key) - 1);
    memcpy(event.payload, payload, payload_len);
    
    // Serialize to string — static to avoid eating task stack (576 bytes)
    static char buffer[RETX_MGR_MAX_EVENT_SIZE + 64];
    _serialize_event(&event, buffer, sizeof(buffer));
    
    // Add to list manager
    int32_t ret = list_mgr_add(&handle->list_mgr, date_key, buffer);
    if (ret != 0) {
        RETX_LOG_ERROR("Failed to add to list manager, ret=%d", ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    RETX_LOG_DEBUG("Failed event added successfully");
    return RETX_MGR_OK;
}

int32_t retx_mgr_process(retx_manager_t *handle) {
    if (!handle || !handle->is_initialized) {
        RETX_LOG_ERROR("Process failed: not initialized");
        return RETX_MGR_ERR_NOT_INITIALIZED;
    }
    
    // Check if enough time has passed since last retry
    uint32_t current_time = _get_current_time_ms();
    if (handle->last_retry_time != 0 && 
        (current_time - handle->last_retry_time) < handle->retry_interval_ms) {
        uint32_t time_remaining = handle->retry_interval_ms - (current_time - handle->last_retry_time);
        RETX_LOG_DEBUG("Too soon to retry, wait %u ms", time_remaining);
        return RETX_MGR_ERR_TOO_SOON;
    }
    
    RETX_LOG_DEBUG("Processing retransmission queue");
    
    // Peek next event — static to avoid eating task stack (576 + 534 bytes)
    static char buffer[RETX_MGR_MAX_EVENT_SIZE + 64];
    list_mgr_read_handle_t read_handle;
    
    int32_t ret = list_mgr_peek_next(&handle->list_mgr, buffer, sizeof(buffer), &read_handle);
    if (ret == RETX_MGR_ERR_NO_DATA) {
        RETX_LOG_DEBUG("No pending events to retransmit");
        return RETX_MGR_ERR_NO_DATA;
    }
    if (ret != 0) {
        RETX_LOG_ERROR("Failed to peek next event, ret=%d", ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    RETX_LOG_DEBUG("Found event to retransmit: date=%s, line=%u", 
        read_handle.date_key, read_handle.line_index);
    
    // Deserialize event — static to avoid eating task stack (534 bytes)
    static retx_event_t event;
    ret = _deserialize_event(buffer, &event);
    if (ret != RETX_MGR_OK) {
        // Corrupted data, acknowledge it to skip
        RETX_LOG_ERROR("Event data corrupted, skipping");
        list_mgr_ack(&handle->list_mgr, &read_handle);
        return RETX_MGR_ERR_LIST_FAILED;
    }
    
    RETX_LOG_DEBUG("Calling send callback: type=%d, payload_len=%u", 
        (int)event.type, event.payload_len);
    
    // Try to send via callback
    ret = handle->send_callback(event.type, event.payload, event.payload_len, handle->user_data);
    
    handle->last_retry_time = _get_current_time_ms();
    
    if (ret == 0) {
        // Success! Acknowledge the event
        RETX_LOG_DEBUG("Event sent successfully, acknowledging");
        list_mgr_ack(&handle->list_mgr, &read_handle);
        return RETX_MGR_OK;
    }
    
    // Send failed, leave it in queue for next retry
    RETX_LOG_DEBUG("Send failed (ret=%d), will retry later", ret);
    return RETX_MGR_ERR_SEND_FAILED;
}

int32_t retx_mgr_get_pending_count(retx_manager_t *handle, uint32_t *count_out) {
    if (!handle || !handle->is_initialized || !count_out) {
        RETX_LOG_ERROR("Get pending count failed: NULL parameter");
        return RETX_MGR_ERR_NULL_PARAM;
    }
    
    // Calculate pending count from metadata
    *count_out = 0;
    
    for (uint32_t i = 0; i < handle->list_mgr.num_tracked_days; i++) {
        uint32_t pending = handle->list_mgr.day_meta[i].total_lines - 
                          handle->list_mgr.day_meta[i].current_index;
        *count_out += pending;
    }
    
    RETX_LOG_DEBUG("Pending event count: %u", *count_out);
    return RETX_MGR_OK;
}

int32_t retx_mgr_cleanup(retx_manager_t *handle) {
    if (!handle || !handle->is_initialized) {
        RETX_LOG_ERROR("Cleanup failed: not initialized");
        return RETX_MGR_ERR_NOT_INITIALIZED;
    }
    
    RETX_LOG_DEBUG("Starting retransmission cleanup");
    int32_t ret = list_mgr_cleanup(&handle->list_mgr);
    
    if (ret == 0) {
        RETX_LOG_DEBUG("Cleanup completed successfully");
    } else {
        RETX_LOG_ERROR("Cleanup failed, ret=%d", ret);
    }
    
    return ret;
}
