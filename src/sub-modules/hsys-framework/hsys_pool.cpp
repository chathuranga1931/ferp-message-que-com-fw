// hsys_pool.cpp
//
// Buffer pool manager implementation.
//
// Pool classes are supplied by the caller at init time as a const table
// (defined in main.c).  No pool sizes are hardcoded here.
//
// A single static backing arena of HSYS_POOL_MAX_BYTES is used for all
// block storage, and a single free-list arena of HSYS_POOL_MAX_BLOCKS
// pointer slots covers all classes.  Both limits are overridable via -D.
// Alloc and free are protected by a critical section (ISR-safe).

#include "hsys_pool.h"
#include "hsys_task_append.h"   // hsys_critical_enter / exit

#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Static arena limits  (override via -D if your pool table is larger)
// ---------------------------------------------------------------------------

#ifndef HSYS_POOL_MAX_BYTES
#define HSYS_POOL_MAX_BYTES   32768U   ///< Maximum total backing store in bytes
#endif

#ifndef HSYS_POOL_MAX_BLOCKS
#define HSYS_POOL_MAX_BLOCKS  128U     ///< Maximum total block count across all classes
#endif

// ---------------------------------------------------------------------------
// Internal pool class descriptor (runtime, not const)
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t  block_size;
    uint16_t  total_count;
    uint16_t  free_count;
    uint8_t  *storage;      ///< Points into s_backing_arena
    void    **free_list;    ///< Points into s_freelist_arena
} hsys_pool_class_t;

// ---------------------------------------------------------------------------
// Static arenas  (zero overhead — no heap used)
// ---------------------------------------------------------------------------

static uint8_t            s_backing_arena[HSYS_POOL_MAX_BYTES];
static void              *s_freelist_arena[HSYS_POOL_MAX_BLOCKS];
static hsys_pool_class_t  s_classes[HSYS_POOL_CLASS_COUNT];
static uint8_t            s_class_count = 0;
static bool               s_initialised = false;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

hsys_status_t hsys_pool_init(const hsys_pool_class_cfg_t *classes,
                              uint8_t class_count)
{
    if (s_initialised)         return HSYS_OK;
    if (classes == nullptr)    return HSYS_ERR_INVALID;
    if (class_count == 0)      return HSYS_ERR_INVALID;
    if (class_count > HSYS_POOL_CLASS_COUNT) return HSYS_ERR_INVALID;

    // Verify the total fits within the static arenas
    uint32_t total_bytes  = 0;
    uint32_t total_blocks = 0;
    for (uint8_t i = 0; i < class_count; i++) {
        total_bytes  += (uint32_t)classes[i].block_size * classes[i].block_count;
        total_blocks += classes[i].block_count;
    }
    if (total_bytes  > HSYS_POOL_MAX_BYTES)  return HSYS_ERR_NO_MEM;
    if (total_blocks > HSYS_POOL_MAX_BLOCKS) return HSYS_ERR_NO_MEM;

    // Carve up the arenas
    uint8_t *backing = s_backing_arena;
    void   **fl      = s_freelist_arena;

    for (uint8_t i = 0; i < class_count; i++) {
        hsys_pool_class_t *cls = &s_classes[i];
        cls->block_size  = classes[i].block_size;
        cls->total_count = classes[i].block_count;
        cls->free_count  = classes[i].block_count;
        cls->storage     = backing;
        cls->free_list   = fl;

        // Populate the free list with a pointer to each block
        for (uint16_t b = 0; b < classes[i].block_count; b++) {
            cls->free_list[b] = backing + (b * classes[i].block_size);
        }

        backing += (uint32_t)classes[i].block_size * classes[i].block_count;
        fl      += classes[i].block_count;
    }

    s_class_count = class_count;
    s_initialised = true;
    return HSYS_OK;
}

// ---------------------------------------------------------------------------
// Alloc
// ---------------------------------------------------------------------------

void *hsys_pool_alloc(uint16_t size)
{
    if (!s_initialised) return nullptr;

    void *block = nullptr;

    hsys_critical_enter();

    // Find the smallest class that fits
    for (uint8_t i = 0; i < s_class_count; i++) {
        hsys_pool_class_t *cls = &s_classes[i];
        if (cls->block_size >= size && cls->free_count > 0) {
            block = cls->free_list[--cls->free_count];
            break;
        }
    }

    hsys_critical_exit();
    return block;
}

// ---------------------------------------------------------------------------
// Free
// ---------------------------------------------------------------------------

void hsys_pool_free(void *ptr)
{
    if (!s_initialised || ptr == nullptr) return;

    hsys_critical_enter();

    // Identify which class this pointer belongs to by address range
    for (uint8_t i = 0; i < s_class_count; i++) {
        hsys_pool_class_t *cls = &s_classes[i];
        uint8_t *base = cls->storage;
        uint8_t *end  = base + (uint32_t)cls->block_size * cls->total_count;

        if ((uint8_t *)ptr >= base && (uint8_t *)ptr < end) {
            if (cls->free_count < cls->total_count) {
                cls->free_list[cls->free_count++] = ptr;
            }
            // else: double-free guard — silently ignore
            break;
        }
    }

    hsys_critical_exit();
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

hsys_status_t hsys_pool_get_info(uint8_t class_index,
                                  hsys_pool_class_info_t *info)
{
    if (class_index >= s_class_count) return HSYS_ERR_INVALID;
    if (info == nullptr)              return HSYS_ERR_NULL;

    hsys_critical_enter();
    info->block_size   = s_classes[class_index].block_size;
    info->total_count  = s_classes[class_index].total_count;
    info->free_count   = s_classes[class_index].free_count;
    hsys_critical_exit();

    return HSYS_OK;
}
