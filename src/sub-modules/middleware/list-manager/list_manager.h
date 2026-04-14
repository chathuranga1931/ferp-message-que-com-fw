#ifndef LIST_MANAGER_H
#define LIST_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

// Maximum number of days that can be tracked
#define LIST_MGR_MAX_TRACKED_DAYS 30

// --- Config ---

typedef struct {
    storage_interface_t *storage;
    const char *parent_path;        // e.g. "/logs"
    const char *file_prefix;        // e.g. "evt"
    
    uint32_t max_lines_per_file;    // Split files when line count exceeds this (e.g., 500)
    uint32_t max_tracked_days;      // How many days to track in metadata (capped at LIST_MGR_MAX_TRACKED_DAYS)
} list_manager_config_t;

// --- Metadata Record (One per day) ---

typedef struct {
    char date_key[16];          // e.g. "20260108"
    uint32_t total_lines;       // Total lines written for this day (across all sequence files)
    uint32_t current_index;     // Read position (0-based line number)
} list_mgr_day_meta_t;

// --- State (Runtime) ---

typedef struct {
    list_manager_config_t config;
    
    // Metadata array: Index 0 = today (day_0), Index 1 = yesterday (day_1), etc.
    list_mgr_day_meta_t day_meta[LIST_MGR_MAX_TRACKED_DAYS];
    uint32_t num_tracked_days;      // Current number of valid entries (0 to max_tracked_days)
    
    // Write cache
    char current_write_date[16];
    uint32_t current_write_seq;     // Current sequence number for today
    uint32_t current_write_lines;   // Lines written to current sequence file
    
    bool is_initialized;
    
} list_manager_t;

// --- Opaque Read Handle (Returned when you peek) ---

typedef struct {
    uint8_t day_index;          // Which day (0=today, 1=yesterday, ...)
    uint32_t line_index;        // Which line in that day
    char date_key[16];
} list_mgr_read_handle_t;

// --- API ---

// Initialize: Loads metadata from storage or creates new
int32_t list_mgr_init(list_manager_t *handle, const list_manager_config_t *config);

// Deinitialize: Saves metadata
int32_t list_mgr_deinit(list_manager_t *handle);

// Add: Appends line to specified date. If date changes, shifts metadata array.
// date_key: e.g. "20260108" (YYYYMMDD format)
// data: The line to append
int32_t list_mgr_add(list_manager_t *handle, const char *date_key, const char *data);

// Peek Next: Returns next unprocessed line. Starts from day_0, moves to day_1 when day_0 is done.
// Returns a handle that you pass to ack() later.
int32_t list_mgr_peek_next(
    list_manager_t *handle, 
    char *out_buf, 
    size_t max_len, 
    list_mgr_read_handle_t *read_handle_out
);

// Acknowledge: Marks the line as processed (increments current_index in metadata).
int32_t list_mgr_ack(list_manager_t *handle, const list_mgr_read_handle_t *read_handle);

// Cleanup: Deletes files for days beyond max_tracked_days
int32_t list_mgr_cleanup(list_manager_t *handle);

#endif // LIST_MANAGER_H
