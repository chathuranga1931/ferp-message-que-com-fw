#include "list_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define LIST_MGR_LOG_DEBUG(fmt, ...) 
#define LIST_MGR_LOG_ERROR(fmt, ...) 

// Error codes
#define LIST_MGR_OK                     0
#define LIST_MGR_ERR_NULL_PARAM         -1
#define LIST_MGR_ERR_NOT_INITIALIZED    -2
#define LIST_MGR_ERR_STORAGE_FAILED     -3
#define LIST_MGR_ERR_NO_DATA            -4
#define LIST_MGR_ERR_INVALID_HANDLE     -5

// Metadata file name
#define METADATA_FILENAME "list_meta.dat"

// Helper: Build full file path
static void _build_file_path(const list_manager_t *handle, const char *date_key, uint32_t seq, char *out_path, size_t max_len) {
    snprintf(out_path, max_len, "%s/%s_%s_%03lu.dat", 
             handle->config.parent_path, 
             handle->config.file_prefix, 
             date_key, 
             seq);
}

// Helper: Build metadata file path
static void _build_metadata_path(const list_manager_t *handle, char *out_path, size_t max_len) {
    snprintf(out_path, max_len, "%s/%s", handle->config.parent_path, METADATA_FILENAME);
}

// Helper: Save metadata to storage
static int32_t _save_metadata(list_manager_t *handle) {
    if (!handle->config.storage || !handle->config.storage->write_file) {
        LIST_MGR_LOG_ERROR("Storage interface not available for metadata save");
        return LIST_MGR_ERR_STORAGE_FAILED;
    }
    
    char meta_path[128];
    _build_metadata_path(handle, meta_path, sizeof(meta_path));
    
    LIST_MGR_LOG_DEBUG("Saving metadata to: %s (days tracked: %u)", meta_path, handle->num_tracked_days);
    
    // Serialize metadata as binary
    char buffer[2048];
    char *ptr = buffer;
    
    // Write num_tracked_days
    uint32_t num_days = handle->num_tracked_days;
    memcpy(ptr, &num_days, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // Write each day's metadata
    for (uint32_t i = 0; i < handle->num_tracked_days; i++) {
        memcpy(ptr, &handle->day_meta[i], sizeof(list_mgr_day_meta_t));
        ptr += sizeof(list_mgr_day_meta_t);
        LIST_MGR_LOG_DEBUG("  Day[%u]: %s - total:%u, index:%u", 
            i, handle->day_meta[i].date_key, 
            handle->day_meta[i].total_lines, 
            handle->day_meta[i].current_index);
    }
    
    // Write cache info
    memcpy(ptr, handle->current_write_date, 16);
    ptr += 16;
    memcpy(ptr, &handle->current_write_seq, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(ptr, &handle->current_write_lines, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    size_t total_size = ptr - buffer;
    LIST_MGR_LOG_DEBUG("Metadata size: %u bytes", total_size);
    
    int32_t ret = handle->config.storage->write_file(meta_path, buffer, 5000);
    if (ret == 0) {
        LIST_MGR_LOG_DEBUG("Metadata saved successfully");
        return LIST_MGR_OK;
    } else {
        LIST_MGR_LOG_ERROR("Failed to save metadata, ret=%d", ret);
        return LIST_MGR_ERR_STORAGE_FAILED;
    }
}

// Helper: Load metadata from storage
static int32_t _load_metadata(list_manager_t *handle) {
    if (!handle->config.storage || !handle->config.storage->read_file) {
        LIST_MGR_LOG_ERROR("Storage interface not available for metadata load");
        return LIST_MGR_ERR_STORAGE_FAILED;
    }
    
    char meta_path[128];
    _build_metadata_path(handle, meta_path, sizeof(meta_path));
    
    LIST_MGR_LOG_DEBUG("Loading metadata from: %s", meta_path);
    
    char buffer[2048];
    int32_t ret = handle->config.storage->read_file(meta_path, buffer, sizeof(buffer), 5000);
    
    if (ret != 0) {
        // Metadata doesn't exist, initialize empty
        LIST_MGR_LOG_DEBUG("Metadata file not found, initializing empty");
        handle->num_tracked_days = 0;
        memset(handle->current_write_date, 0, sizeof(handle->current_write_date));
        handle->current_write_seq = 0;
        handle->current_write_lines = 0;
        return LIST_MGR_OK;
    }
    
    // Deserialize
    char *ptr = buffer;
    
    memcpy(&handle->num_tracked_days, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    LIST_MGR_LOG_DEBUG("Loaded %u tracked days", handle->num_tracked_days);
    
    if (handle->num_tracked_days > handle->config.max_tracked_days) {
        LIST_MGR_LOG_DEBUG("Capping tracked days from %u to %u", 
            handle->num_tracked_days, handle->config.max_tracked_days);
        handle->num_tracked_days = handle->config.max_tracked_days;
    }
    
    for (uint32_t i = 0; i < handle->num_tracked_days; i++) {
        memcpy(&handle->day_meta[i], ptr, sizeof(list_mgr_day_meta_t));
        ptr += sizeof(list_mgr_day_meta_t);
        LIST_MGR_LOG_DEBUG("  Day[%u]: %s - total:%u, index:%u", 
            i, handle->day_meta[i].date_key, 
            handle->day_meta[i].total_lines, 
            handle->day_meta[i].current_index);
    }
    
    memcpy(handle->current_write_date, ptr, 16);
    ptr += 16;
    memcpy(&handle->current_write_seq, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(&handle->current_write_lines, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    LIST_MGR_LOG_DEBUG("Write cache: date=%s, seq=%u, lines=%u", 
        handle->current_write_date, handle->current_write_seq, handle->current_write_lines);
    
    return LIST_MGR_OK;
}

// Helper: Shift metadata array (new day detected)
static void _shift_metadata(list_manager_t *handle, const char *new_date_key) {
    LIST_MGR_LOG_DEBUG("New day detected: %s (previous: %s)", 
        new_date_key, handle->current_write_date);
    
    // Shift all entries down
    for (int32_t i = LIST_MGR_MAX_TRACKED_DAYS - 1; i > 0; i--) {
        if (i < handle->config.max_tracked_days) {
            handle->day_meta[i] = handle->day_meta[i - 1];
        }
    }
    
    // Initialize day_0 with new date
    memset(&handle->day_meta[0], 0, sizeof(list_mgr_day_meta_t));
    strncpy(handle->day_meta[0].date_key, new_date_key, sizeof(handle->day_meta[0].date_key) - 1);
    handle->day_meta[0].total_lines = 0;
    handle->day_meta[0].current_index = 0;
    
    // Update tracked count
    if (handle->num_tracked_days < handle->config.max_tracked_days) {
        handle->num_tracked_days++;
    }
    
    LIST_MGR_LOG_DEBUG("Shifted metadata, now tracking %u days", handle->num_tracked_days);
    
    // Reset write cache
    strncpy(handle->current_write_date, new_date_key, sizeof(handle->current_write_date) - 1);
    handle->current_write_seq = 0;
    handle->current_write_lines = 0;
}

// === Public API Implementation ===

int32_t list_mgr_init(list_manager_t *handle, const list_manager_config_t *config) {
    if (!handle || !config || !config->storage) {
        LIST_MGR_LOG_ERROR("Init failed: NULL parameter");
        return LIST_MGR_ERR_NULL_PARAM;
    }
    
    LIST_MGR_LOG_DEBUG("Initializing list manager:");
    LIST_MGR_LOG_DEBUG("  parent_path: %s", config->parent_path);
    LIST_MGR_LOG_DEBUG("  file_prefix: %s", config->file_prefix);
    LIST_MGR_LOG_DEBUG("  max_lines_per_file: %u", config->max_lines_per_file);
    LIST_MGR_LOG_DEBUG("  max_tracked_days: %u", config->max_tracked_days);
    
    memset(handle, 0, sizeof(list_manager_t));
    memcpy(&handle->config, config, sizeof(list_manager_config_t));
    
    // Validate and cap max_tracked_days
    if (handle->config.max_tracked_days > LIST_MGR_MAX_TRACKED_DAYS) {
        LIST_MGR_LOG_DEBUG("Capping max_tracked_days from %u to %u", 
            handle->config.max_tracked_days, LIST_MGR_MAX_TRACKED_DAYS);
        handle->config.max_tracked_days = LIST_MGR_MAX_TRACKED_DAYS;
    }
    
    // Load metadata from storage
    int32_t ret = _load_metadata(handle);
    if (ret != LIST_MGR_OK) {
        LIST_MGR_LOG_ERROR("Failed to load metadata, ret=%d", ret);
        return ret;
    }
    
    handle->is_initialized = true;
    LIST_MGR_LOG_DEBUG("List manager initialized successfully");
    return LIST_MGR_OK;
}

int32_t list_mgr_deinit(list_manager_t *handle) {
    if (!handle || !handle->is_initialized) {
        LIST_MGR_LOG_ERROR("Deinit failed: not initialized");
        return LIST_MGR_ERR_NOT_INITIALIZED;
    }
    
    LIST_MGR_LOG_DEBUG("Deinitializing list manager");
    
    // Save metadata
    _save_metadata(handle);
    
    handle->is_initialized = false;
    LIST_MGR_LOG_DEBUG("List manager deinitialized");
    return LIST_MGR_OK;
}

int32_t list_mgr_add(list_manager_t *handle, const char *date_key, const char *data) {
    if (!handle || !handle->is_initialized || !date_key || !data) {
        LIST_MGR_LOG_ERROR("Add failed: NULL parameter");
        return LIST_MGR_ERR_NULL_PARAM;
    }
    
    LIST_MGR_LOG_DEBUG("Adding entry for date: %s", date_key);
    
    // Check if date changed
    if (handle->num_tracked_days == 0 || strcmp(handle->current_write_date, date_key) != 0) {
        _shift_metadata(handle, date_key);
    }
    
    // Check if need to create new sequence file
    if (handle->current_write_lines >= handle->config.max_lines_per_file) {
        LIST_MGR_LOG_DEBUG("Max lines reached, creating new sequence file (seq: %u -> %u)", 
            handle->current_write_seq, handle->current_write_seq + 1);
        handle->current_write_seq++;
        handle->current_write_lines = 0;
    }
    
    // Build file path
    char file_path[128];
    _build_file_path(handle, date_key, handle->current_write_seq, file_path, sizeof(file_path));
    
    LIST_MGR_LOG_DEBUG("Writing to file: %s (line %u)", file_path, handle->current_write_lines);
    
    // Append line
    int32_t ret = handle->config.storage->append_line(file_path, data, 5000);
    if (ret != 0) {
        LIST_MGR_LOG_ERROR("Failed to append line, ret=%d", ret);
        return LIST_MGR_ERR_STORAGE_FAILED;
    }
    
    // Update metadata
    handle->day_meta[0].total_lines++;
    handle->current_write_lines++;
    
    LIST_MGR_LOG_DEBUG("Entry added successfully (total lines: %u)", handle->day_meta[0].total_lines);
    
    // Save metadata periodically (every 10 lines for safety)
    if (handle->day_meta[0].total_lines % 10 == 0) {
        LIST_MGR_LOG_DEBUG("Periodic metadata save (every 10 lines)");
        _save_metadata(handle);
    }
    
    return LIST_MGR_OK;
}

int32_t list_mgr_peek_next(
    list_manager_t *handle, 
    char *out_buf, 
    size_t max_len, 
    list_mgr_read_handle_t *read_handle_out
) {
    if (!handle || !handle->is_initialized || !out_buf || !read_handle_out) {
        LIST_MGR_LOG_ERROR("Peek failed: NULL parameter");
        return LIST_MGR_ERR_NULL_PARAM;
    }
    
    LIST_MGR_LOG_DEBUG("Peeking next unprocessed line");
    
    // Find the oldest day with unprocessed lines
    for (uint32_t day_idx = handle->num_tracked_days; day_idx > 0; day_idx--) {
        uint32_t i = day_idx - 1;
        
        if (handle->day_meta[i].current_index < handle->day_meta[i].total_lines) {
            // Found unprocessed line
            uint32_t line_idx = handle->day_meta[i].current_index;
            
            // Calculate which file and which line within that file
            uint32_t seq = line_idx / handle->config.max_lines_per_file;
            uint32_t line_in_file = line_idx % handle->config.max_lines_per_file;
            
            LIST_MGR_LOG_DEBUG("Found unprocessed line in day[%u] (%s): line %u (seq=%u, line_in_file=%u)", 
                i, handle->day_meta[i].date_key, line_idx, seq, line_in_file);
            
            // Build file path
            char file_path[128];
            _build_file_path(handle, handle->day_meta[i].date_key, seq, file_path, sizeof(file_path));
            
            LIST_MGR_LOG_DEBUG("Reading from: %s", file_path);
            
            // Read line
            int32_t ret = handle->config.storage->read_line(file_path, line_in_file, out_buf, max_len, 5000);
            if (ret != 0) {
                LIST_MGR_LOG_ERROR("Failed to read line, ret=%d", ret);
                return LIST_MGR_ERR_STORAGE_FAILED;
            }
            
            // Fill read handle
            read_handle_out->day_index = i;
            read_handle_out->line_index = line_idx;
            strncpy(read_handle_out->date_key, handle->day_meta[i].date_key, sizeof(read_handle_out->date_key) - 1);
            
            LIST_MGR_LOG_DEBUG("Peek successful");
            return LIST_MGR_OK;
        }
    }
    
    LIST_MGR_LOG_DEBUG("No unprocessed lines found");
    return LIST_MGR_ERR_NO_DATA;
}

int32_t list_mgr_ack(list_manager_t *handle, const list_mgr_read_handle_t *read_handle) {
    if (!handle || !handle->is_initialized || !read_handle) {
        LIST_MGR_LOG_ERROR("Ack failed: NULL parameter");
        return LIST_MGR_ERR_NULL_PARAM;
    }
    
    if (read_handle->day_index >= handle->num_tracked_days) {
        LIST_MGR_LOG_ERROR("Ack failed: invalid day_index %u (max: %u)", 
            read_handle->day_index, handle->num_tracked_days);
        return LIST_MGR_ERR_INVALID_HANDLE;
    }
    
    LIST_MGR_LOG_DEBUG("Acknowledging line: day[%u] (%s), line %u", 
        read_handle->day_index, read_handle->date_key, read_handle->line_index);
    
    // Increment current_index for that day
    handle->day_meta[read_handle->day_index].current_index++;
    
    LIST_MGR_LOG_DEBUG("Day[%u] progress: %u/%u", 
        read_handle->day_index,
        handle->day_meta[read_handle->day_index].current_index,
        handle->day_meta[read_handle->day_index].total_lines);
    
    // Save metadata
    _save_metadata(handle);
    
    return LIST_MGR_OK;
}

int32_t list_mgr_cleanup(list_manager_t *handle) {
    if (!handle || !handle->is_initialized) {
        LIST_MGR_LOG_ERROR("Cleanup failed: not initialized");
        return LIST_MGR_ERR_NOT_INITIALIZED;
    }
    
    LIST_MGR_LOG_DEBUG("Starting cleanup (max_tracked_days: %u, current: %u)", 
        handle->config.max_tracked_days, handle->num_tracked_days);
    
    // Delete files for days beyond max_tracked_days
    uint32_t deleted_count = 0;
    for (uint32_t i = handle->config.max_tracked_days; i < handle->num_tracked_days; i++) {
        LIST_MGR_LOG_DEBUG("Cleaning up day[%u]: %s", i, handle->day_meta[i].date_key);
        
        // Build file pattern and delete all sequence files for this day
        for (uint32_t seq = 0; seq < 1000; seq++) {  // Reasonable upper limit
            char file_path[128];
            _build_file_path(handle, handle->day_meta[i].date_key, seq, file_path, sizeof(file_path));
            
            // Try to delete (will fail silently if doesn't exist)
            int32_t ret = handle->config.storage->delete_file(file_path, 5000);
            if (ret == 0) {
                deleted_count++;
                LIST_MGR_LOG_DEBUG("  Deleted: %s", file_path);
            }
        }
    }
    
    LIST_MGR_LOG_DEBUG("Cleanup complete, deleted %u files", deleted_count);
    return LIST_MGR_OK;
}
