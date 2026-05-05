// retransmission_manager.cpp
//
// See retransmission_manager.h for description.

#include "retransmission_manager.h"
#include "pal_logger.h"
#include "pal_time.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __TAG__  "RETX_MGR"

// ---------------------------------------------------------------------------
// Serialisation helpers
// Format written to list_manager: "TYPE|LEN|<payload bytes>"
// ---------------------------------------------------------------------------

static void _serialize_event(const retx_event_t *event,
                              char *out_buf, size_t max_len)
{
    int header_len = snprintf(out_buf, max_len, "%d|%u|",
                              (int)event->type, (unsigned)event->payload_len);
    if (header_len > 0 && (size_t)(header_len + event->payload_len) < max_len) {
        memcpy(out_buf + header_len, event->payload, event->payload_len);
    }
}

static int32_t _deserialize_event(const char *buf, retx_event_t *event)
{
    int      type_int;
    unsigned payload_len;

    if (sscanf(buf, "%d|%u|", &type_int, &payload_len) != 2) {
        return RETX_MGR_ERR_LIST_FAILED;
    }
    if (payload_len > RETX_MGR_MAX_EVENT_SIZE) {
        return RETX_MGR_ERR_LIST_FAILED;
    }

    event->type        = (retx_event_type_t)type_int;
    event->payload_len = (uint16_t)payload_len;

    // Locate payload start (after second '|')
    const char *p = buf;
    int pipes = 0;
    while (*p && pipes < 2) {
        if (*p++ == '|') pipes++;
    }
    if (pipes < 2) {
        return RETX_MGR_ERR_LIST_FAILED;
    }

    memcpy(event->payload, p, event->payload_len);
    return RETX_MGR_OK;
}

// ---------------------------------------------------------------------------
// Platform time helper
// ---------------------------------------------------------------------------

static uint32_t _now_ms(void)
{
    return (uint32_t)(pal_time_get_ms() & 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int32_t retx_mgr_init(retx_manager_t *handle, const retx_manager_config_t *config)
{
    if (!handle || !config || !config->send_callback) {
        return RETX_MGR_ERR_NULL_PARAM;
    }

    memset(handle, 0, sizeof(retx_manager_t));

    int32_t ret = list_mgr_init(&handle->list_mgr, &config->list_config);
    if (ret != 0) {
        LOG_MSG_ERROR(true, "retx: list_mgr_init failed (%ld)", (long)ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }

    handle->send_callback      = config->send_callback;
    handle->user_data          = config->user_data;
    handle->retry_interval_ms  = config->retry_interval_ms;
    handle->last_retry_time_ms = 0;
    handle->is_initialized     = true;

    LOG_MSG_INFO(true, "retx: initialized (retry=%lums)", (unsigned long)config->retry_interval_ms);
    return RETX_MGR_OK;
}

int32_t retx_mgr_deinit(retx_manager_t *handle)
{
    if (!handle || !handle->is_initialized) {
        return RETX_MGR_ERR_NOT_INIT;
    }
    list_mgr_deinit(&handle->list_mgr);
    handle->is_initialized = false;
    return RETX_MGR_OK;
}

int32_t retx_mgr_add_failed_event(retx_manager_t   *handle,
                                   const char       *date_key,
                                   retx_event_type_t type,
                                   const char       *payload,
                                   size_t            payload_len)
{
    if (!handle || !handle->is_initialized || !date_key || !payload) {
        return RETX_MGR_ERR_NULL_PARAM;
    }
    if (payload_len > RETX_MGR_MAX_EVENT_SIZE) {
        LOG_MSG_ERROR(true, "retx: payload too large (%u bytes)", (unsigned)payload_len);
        return RETX_MGR_ERR_NULL_PARAM;
    }

    retx_event_t event;
    event.type        = type;
    event.payload_len = (uint16_t)payload_len;
    strncpy(event.date_key, date_key, sizeof(event.date_key) - 1);
    event.date_key[sizeof(event.date_key) - 1] = '\0';
    memcpy(event.payload, payload, payload_len);

    char buf[RETX_MGR_MAX_EVENT_SIZE + 64];
    _serialize_event(&event, buf, sizeof(buf));

    int32_t ret = list_mgr_add(&handle->list_mgr, date_key, buf);
    if (ret != 0) {
        LOG_MSG_ERROR(true, "retx: list_mgr_add failed (%ld)", (long)ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }

    LOG_MSG_INFO(true, "retx: stored type=%d date=%s len=%u", (int)type, date_key, (unsigned)payload_len);
    return RETX_MGR_OK;
}

int32_t retx_mgr_process(retx_manager_t *handle)
{
    if (!handle || !handle->is_initialized) {
        return RETX_MGR_ERR_NOT_INIT;
    }

    uint32_t now = _now_ms();
    if (handle->last_retry_time_ms != 0 &&
        (now - handle->last_retry_time_ms) < handle->retry_interval_ms) {
        return RETX_MGR_ERR_TOO_SOON;
    }

    char                 buf[RETX_MGR_MAX_EVENT_SIZE + 64];
    list_mgr_read_handle_t rh;

    int32_t ret = list_mgr_peek_next(&handle->list_mgr, buf, sizeof(buf), &rh);
    if (ret == -4) {  // LIST_MGR_ERR_NO_DATA
        return RETX_MGR_ERR_NO_DATA;
    }
    if (ret != 0) {
        LOG_MSG_ERROR(true, "retx: peek_next failed (%ld)", (long)ret);
        return RETX_MGR_ERR_LIST_FAILED;
    }

    retx_event_t event;
    if (_deserialize_event(buf, &event) != RETX_MGR_OK) {
        // Corrupted line — skip it
        LOG_MSG_ERROR(true, "retx: corrupted record, skipping");
        list_mgr_ack(&handle->list_mgr, &rh);
        return RETX_MGR_ERR_LIST_FAILED;
    }

    ret = handle->send_callback(event.type, event.payload, event.payload_len, handle->user_data);
    handle->last_retry_time_ms = _now_ms();

    if (ret == 0) {
        list_mgr_ack(&handle->list_mgr, &rh);
        LOG_MSG_INFO(true, "retx: sent OK (type=%d)", (int)event.type);
        return RETX_MGR_OK;
    }

    LOG_MSG_WARNING(true, "retx: send failed (ret=%ld), will retry", (long)ret);
    return RETX_MGR_ERR_SEND_FAILED;
}

int32_t retx_mgr_get_pending_count(retx_manager_t *handle, uint32_t *count_out)
{
    if (!handle || !handle->is_initialized || !count_out) {
        return RETX_MGR_ERR_NULL_PARAM;
    }
    *count_out = 0;
    for (uint32_t i = 0; i < handle->list_mgr.num_tracked_days; i++) {
        uint32_t total = handle->list_mgr.day_meta[i].total_lines;
        uint32_t done  = handle->list_mgr.day_meta[i].current_index;
        *count_out += (total > done) ? (total - done) : 0;
    }
    return RETX_MGR_OK;
}

int32_t retx_mgr_cleanup(retx_manager_t *handle)
{
    if (!handle || !handle->is_initialized) {
        return RETX_MGR_ERR_NOT_INIT;
    }
    return list_mgr_cleanup(&handle->list_mgr);
}
