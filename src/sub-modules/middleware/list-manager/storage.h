#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t (*storage_delete_file_t)(const char* file_path, uint32_t timeout_ms);
typedef int32_t (*storage_create_file_t)(const char* file_path, uint32_t timeout_ms);
typedef int32_t (*storage_append_line_t)(const char* file_path, const char* line, uint32_t timeout_ms);
typedef int32_t (*storage_write_file_t)(const char* file_path, const char* content, uint32_t timeout_ms);
typedef int32_t (*storage_read_file_t)(const char* file_path, char* content, size_t max_len, uint32_t timeout_ms);
typedef int32_t (*storage_remove_dir_t)(const char* path, uint32_t timeout_ms);
typedef int32_t (*storage_read_line_t)(const char* path, uint32_t line, char* read_line, size_t max_len, uint32_t timeout_ms);

// Get next file in directory (for iterating through files)
// file_idx: pointer to index, set to 0 to start, function updates it, 0xFFFFFFFF when done
// file_path: output buffer for the file path
typedef int32_t (*storage_get_next_file_t)(char* file_path, size_t max_len, uint32_t* file_idx, uint32_t timeout_ms);

typedef struct {
    storage_delete_file_t delete_file;
    storage_create_file_t create_file;
    storage_append_line_t append_line;
    storage_write_file_t write_file;
    storage_read_file_t read_file;
    storage_remove_dir_t remove_dir;
    storage_read_line_t read_line;
    storage_get_next_file_t get_next_file;
} storage_interface_t;

#endif // STORAGE_H
