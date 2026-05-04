// hsys_device_info.h
//
// Middleware layer for device identity data.
//
// Device info is runtime-only: values are never stored to flash, have no
// compile-time defaults, and are written by exactly the module(s) listed in
// each entry's write-permission table.  A valid flag tracks whether each
// field has been populated.
//
// Table entry layout:
//   key              — 16-bit application-defined identifier  (DEV_INFO_KEY_*)
//   name[32]         — human-readable name (used in logging only)
//   write_perm       — const pointer to array of module IDs allowed to write
//   write_perm_count — length of write_perm array
//   read_perm        — const pointer to array of module IDs allowed to read
//                      (NULL = any module may read)
//   read_perm_count  — length of read_perm array (0 when read_perm is NULL)
//   type             — HSYS_TYPE_STRING | UINT32 | BOOL
//   p_value          — pointer to the live variable in app_device_info_t
//   max_length       — buffer size in bytes (strings) or sizeof for scalars
//   is_valid         — runtime flag; false until a permitted writer sets it

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hsys_type.h"
#include "hsys_types.h"   // hsys_module_id_t

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

#ifndef ERROR_OK
#error "Define ERROR_OK in user_config.h"
#endif
#ifndef ERR_DEV_INFO_OFFSET
#error "Define ERR_DEV_INFO_OFFSET in user_config.h"
#endif

#define DEV_INFO_SUCCESS            (ERROR_OK)
#define DEV_INFO_UNINIT             (ERR_DEV_INFO_OFFSET + 0)
#define DEV_INFO_NULL               (ERR_DEV_INFO_OFFSET + 1)
#define DEV_INFO_NOT_FOUND          (ERR_DEV_INFO_OFFSET + 2)
#define DEV_INFO_PERM_DENIED        (ERR_DEV_INFO_OFFSET + 3)
#define DEV_INFO_BUFFER_TOO_SMALL   (ERR_DEV_INFO_OFFSET + 4)
#define DEV_INFO_INVALID_TYPE       (ERR_DEV_INFO_OFFSET + 5)
#define DEV_INFO_NOT_VALID          (ERR_DEV_INFO_OFFSET + 6)  ///< field not yet populated

// ---------------------------------------------------------------------------
// Table entry
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t                    key;
    char                        name[32];
    const hsys_module_id_t     *write_perm;         ///< allowed writers; NULL = none
    uint8_t                     write_perm_count;
    const hsys_module_id_t     *read_perm;           ///< allowed readers; NULL = any
    uint8_t                     read_perm_count;
    hsys_type_t                 type;
    void                       *p_value;
    uint32_t                    max_length;
    bool                        is_valid;            ///< runtime validity flag
} dev_info_entry_t;

// ---------------------------------------------------------------------------
// Handle
// ---------------------------------------------------------------------------

typedef struct {
    bool                is_initialized;
    uint16_t            entry_count;
    dev_info_entry_t   *table;          ///< non-const — is_valid is mutated at runtime
} dev_info_handle_t;

// ---------------------------------------------------------------------------
// Init descriptor (passed at init time)
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t            entry_count;
    dev_info_entry_t   *table;
} dev_info_init_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/** Bind a table to a handle. */
int32_t hsys_dev_info_init(dev_info_init_t init, dev_info_handle_t *handle);

/**
 * Write a value to a device info field.
 * @param writer_id  Module ID of the caller — checked against write_perm.
 * @param key        DEV_INFO_KEY_* identifier.
 * @param src        Pointer to the source data.
 * @param src_len    Length of src in bytes (checked against max_length for strings).
 * @return DEV_INFO_SUCCESS, DEV_INFO_PERM_DENIED, DEV_INFO_NOT_FOUND, etc.
 */
int32_t hsys_dev_info_write(dev_info_handle_t *handle,
                             hsys_module_id_t   writer_id,
                             uint16_t           key,
                             const void        *src,
                             size_t             src_len);

/**
 * Read a value from a device info field.
 * @param reader_id  Module ID of the caller — checked against read_perm (if set).
 * @param key        DEV_INFO_KEY_* identifier.
 * @param dst        Destination buffer.
 * @param dst_len    Size of dst in bytes.
 * @param out_type   Optional — filled with the field's hsys_type_t.
 * @return DEV_INFO_SUCCESS, DEV_INFO_PERM_DENIED, DEV_INFO_NOT_FOUND,
 *         DEV_INFO_NOT_VALID, DEV_INFO_BUFFER_TOO_SMALL.
 */
int32_t hsys_dev_info_read(dev_info_handle_t *handle,
                            hsys_module_id_t   reader_id,
                            uint16_t           key,
                            void              *dst,
                            size_t             dst_len,
                            hsys_type_t       *out_type);

/** Returns true if the field identified by key has been written at least once. */
bool hsys_dev_info_is_valid(dev_info_handle_t *handle, uint16_t key);

#ifdef __cplusplus
}
#endif
