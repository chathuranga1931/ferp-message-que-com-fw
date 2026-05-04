// hsys_device_info.cpp
//
// Middleware implementation for device identity data.

#include "hsys_device_info.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "HS_DINFO"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dev_info_entry_t *_find_entry(dev_info_handle_t *h, uint16_t key)
{
    for (uint16_t i = 0; i < h->entry_count; i++) {
        if (h->table[i].key == key) return &h->table[i];
    }
    return nullptr;
}

static bool _perm_ok(const hsys_module_id_t *table, uint8_t count, hsys_module_id_t id)
{
    if (table == nullptr) return true;   // NULL table = open access
    for (uint8_t i = 0; i < count; i++) {
        if (table[i] == id) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

int32_t hsys_dev_info_init(dev_info_init_t init, dev_info_handle_t *handle)
{
    if (!handle)           return DEV_INFO_NULL;
    if (!init.table)       return DEV_INFO_NULL;
    if (!init.entry_count) return DEV_INFO_NULL;

    handle->table        = init.table;
    handle->entry_count  = init.entry_count;
    handle->is_initialized = true;
    return DEV_INFO_SUCCESS;
}

int32_t hsys_dev_info_write(dev_info_handle_t *handle,
                             hsys_module_id_t   writer_id,
                             uint16_t           key,
                             const void        *src,
                             size_t             src_len)
{
    if (!handle || !src)           return DEV_INFO_NULL;
    if (!handle->is_initialized)   return DEV_INFO_UNINIT;

    dev_info_entry_t *e = _find_entry(handle, key);
    if (!e) return DEV_INFO_NOT_FOUND;

    if (!_perm_ok(e->write_perm, e->write_perm_count, writer_id)) {
        return DEV_INFO_PERM_DENIED;
    }

    switch (e->type) {
        case HSYS_TYPE_STRING:
            if (src_len > e->max_length) return DEV_INFO_BUFFER_TOO_SMALL;
            strncpy(static_cast<char *>(e->p_value),
                    static_cast<const char *>(src),
                    e->max_length - 1);
            static_cast<char *>(e->p_value)[e->max_length - 1] = '\0';
            break;
        case HSYS_TYPE_UINT32:
            if (src_len < sizeof(uint32_t)) return DEV_INFO_BUFFER_TOO_SMALL;
            memcpy(e->p_value, src, sizeof(uint32_t));
            break;
        case HSYS_TYPE_BOOL:
            if (src_len < sizeof(bool)) return DEV_INFO_BUFFER_TOO_SMALL;
            memcpy(e->p_value, src, sizeof(bool));
            break;
        default:
            return DEV_INFO_INVALID_TYPE;
    }

    e->is_valid = true;
    return DEV_INFO_SUCCESS;
}

int32_t hsys_dev_info_read(dev_info_handle_t *handle,
                            hsys_module_id_t   reader_id,
                            uint16_t           key,
                            void              *dst,
                            size_t             dst_len,
                            hsys_type_t       *out_type)
{
    if (!handle || !dst)           return DEV_INFO_NULL;
    if (!handle->is_initialized)   return DEV_INFO_UNINIT;

    dev_info_entry_t *e = _find_entry(handle, key);
    if (!e) return DEV_INFO_NOT_FOUND;

    if (!_perm_ok(e->read_perm, e->read_perm_count, reader_id)) {
        return DEV_INFO_PERM_DENIED;
    }

    if (!e->is_valid) return DEV_INFO_NOT_VALID;

    if (out_type) *out_type = e->type;

    switch (e->type) {
        case HSYS_TYPE_STRING: {
            size_t val_len = strlen(static_cast<const char *>(e->p_value)) + 1;
            if (dst_len < val_len) return DEV_INFO_BUFFER_TOO_SMALL;
            memcpy(dst, e->p_value, val_len);
            break;
        }
        case HSYS_TYPE_UINT32:
            if (dst_len < sizeof(uint32_t)) return DEV_INFO_BUFFER_TOO_SMALL;
            memcpy(dst, e->p_value, sizeof(uint32_t));
            break;
        case HSYS_TYPE_BOOL:
            if (dst_len < sizeof(bool)) return DEV_INFO_BUFFER_TOO_SMALL;
            memcpy(dst, e->p_value, sizeof(bool));
            break;
        default:
            return DEV_INFO_INVALID_TYPE;
    }

    return DEV_INFO_SUCCESS;
}

bool hsys_dev_info_is_valid(dev_info_handle_t *handle, uint16_t key)
{
    if (!handle || !handle->is_initialized) return false;
    dev_info_entry_t *e = _find_entry(handle, key);
    return e ? e->is_valid : false;
}
