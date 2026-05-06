// hsys_msg.cpp
//
// Message bus — descriptor table, creation, ref-counted delivery.
//
// Key changes from the previous design:
//   • hsys_msg_table_init() registers the app descriptor table (rodata).
//   • hsys_msg_create() looks up the descriptor, allocates the payload
//     from the pool, and allocates the hsys_msg_t header from a separate
//     small pool of message-header slots.
//   • Task queues carry hsys_msg_t* (one pointer per subscriber, zero-copy).
//   • hsys_msg_release() decrements ref_count; frees payload + header on zero.

#include "hsys_msg.h"
#include "hsys_pool.h"
#include "hsys_task_mgr.h"
#include "hsys_mutex.h"
#include "hsys_task_append.h"
#include "pal_logger.h"

#define __TAG__ "HSYS_MSG"
#ifndef HSYS_MSG_LOG_EN
#define HSYS_MSG_LOG_EN true
#endif

#include <string.h>

// ---------------------------------------------------------------------------
// Message-header pool  (statically allocated slab of hsys_msg_t instances)
// ---------------------------------------------------------------------------

#ifndef HSYS_MSG_HEADER_POOL_SIZE
#define HSYS_MSG_HEADER_POOL_SIZE   32   ///< Max in-flight messages at any time
#endif

static hsys_msg_t           s_header_pool[HSYS_MSG_HEADER_POOL_SIZE];
static bool                 s_header_used[HSYS_MSG_HEADER_POOL_SIZE];
static uint8_t              s_header_peak = 0;   // peak used slots (watermark)
static hsys_mutex_handle_t  s_header_mutex = nullptr;

static hsys_msg_t *header_alloc(void)
{
    hsys_mutex_lock(s_header_mutex);
    for (uint8_t i = 0; i < HSYS_MSG_HEADER_POOL_SIZE; i++) {
        if (!s_header_used[i]) {
            s_header_used[i] = true;

            // Update peak watermark
            uint8_t used = 0;
            for (uint8_t j = 0; j < HSYS_MSG_HEADER_POOL_SIZE; j++)
                if (s_header_used[j]) used++;
            if (used > s_header_peak) s_header_peak = used;

            hsys_mutex_unlock(s_header_mutex);
            return &s_header_pool[i];
        }
    }
    hsys_mutex_unlock(s_header_mutex);
    return nullptr;  // all slots in use
}

static void header_free(hsys_msg_t *msg)
{
    hsys_mutex_lock(s_header_mutex);
    for (uint8_t i = 0; i < HSYS_MSG_HEADER_POOL_SIZE; i++) {
        if (&s_header_pool[i] == msg) {
            s_header_used[i] = false;
            break;
        }
    }
    hsys_mutex_unlock(s_header_mutex);
}

// ---------------------------------------------------------------------------
// Descriptor table
// ---------------------------------------------------------------------------

static const hsys_msg_desc_t   *s_desc_table       = nullptr;
static uint16_t                 s_desc_table_count  = 0;

static const hsys_msg_desc_t *find_desc(hsys_msg_id_t msg_id)
{
    for (uint16_t i = 0; i < s_desc_table_count; i++) {
        if (s_desc_table[i].msg_id == msg_id) return &s_desc_table[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Subscription table
// ---------------------------------------------------------------------------

typedef struct {
    hsys_module_id_t subscribers[HSYS_MAX_SUBSCRIBERS_PER_MSG];
    uint8_t          count;
} hsys_sub_entry_t;

static hsys_sub_entry_t    s_sub_table[HSYS_MAX_MSG_IDS];
static hsys_mutex_handle_t s_sub_mutex    = nullptr;
static bool                s_initialised  = false;

static hsys_sub_entry_t *sub_entry_for(hsys_msg_id_t msg_id)
{
    if (msg_id == HSYS_MSG_ID_INVALID || msg_id >= HSYS_MAX_MSG_IDS)
        return nullptr;
    return &s_sub_table[msg_id];
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_init(void)
{
    if (s_initialised) return HSYS_OK;

    memset(s_sub_table,   0, sizeof(s_sub_table));
    memset(s_header_pool, 0, sizeof(s_header_pool));
    memset(s_header_used, 0, sizeof(s_header_used));

    s_sub_mutex    = hsys_mutex_create();
    s_header_mutex = hsys_mutex_create();

    if (!s_sub_mutex || !s_header_mutex) return HSYS_ERR_NO_MEM;

    s_initialised = true;
    return HSYS_OK;
}

hsys_status_t hsys_msg_table_init(const hsys_msg_desc_t *table, uint16_t count)
{
    if (!s_initialised)        return HSYS_ERR_NOT_INIT;
    if (!table || count == 0)  return HSYS_ERR_INVALID;

    s_desc_table       = table;
    s_desc_table_count = count;
    return HSYS_OK;
}

// ---------------------------------------------------------------------------
// Subscribe / Unsubscribe
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_subscribe(hsys_msg_id_t msg_id, hsys_module_id_t module_id)
{
    if (!s_initialised) return HSYS_ERR_NOT_INIT;

    hsys_sub_entry_t *e = sub_entry_for(msg_id);
    if (!e) return HSYS_ERR_INVALID;

    hsys_mutex_lock(s_sub_mutex);
    for (uint8_t i = 0; i < e->count; i++) {
        if (e->subscribers[i] == module_id) {
            hsys_mutex_unlock(s_sub_mutex);
            return HSYS_ERR_ALREADY_EXISTS;
        }
    }
    if (e->count >= HSYS_MAX_SUBSCRIBERS_PER_MSG) {
        hsys_mutex_unlock(s_sub_mutex);
        return HSYS_ERR_NO_MEM;
    }
    e->subscribers[e->count++] = module_id;
    hsys_mutex_unlock(s_sub_mutex);
    return HSYS_OK;
}

hsys_status_t hsys_msg_unsubscribe(hsys_msg_id_t msg_id, hsys_module_id_t module_id)
{
    if (!s_initialised) return HSYS_ERR_NOT_INIT;

    hsys_sub_entry_t *e = sub_entry_for(msg_id);
    if (!e) return HSYS_ERR_INVALID;

    hsys_mutex_lock(s_sub_mutex);
    for (uint8_t i = 0; i < e->count; i++) {
        if (e->subscribers[i] == module_id) {
            e->subscribers[i] = e->subscribers[--e->count];
            hsys_mutex_unlock(s_sub_mutex);
            return HSYS_OK;
        }
    }
    hsys_mutex_unlock(s_sub_mutex);
    return HSYS_ERR_NOT_FOUND;
}

// ---------------------------------------------------------------------------
// Message create
// ---------------------------------------------------------------------------

hsys_msg_t *hsys_msg_create(hsys_msg_id_t msg_id, hsys_module_id_t sender_id)
{
    if (!s_initialised) return nullptr;

    const hsys_msg_desc_t *desc = find_desc(msg_id);
    if (!desc) {
        LOG_MSG_ERROR(HSYS_MSG_LOG_EN, "create: unknown msg_id=0x%04X", msg_id);
        return nullptr;
    }

    hsys_msg_t *msg = header_alloc();
    if (!msg) {
        LOG_MSG_ERROR(HSYS_MSG_LOG_EN, "create: header pool exhausted");
        return nullptr;
    }

    memset(msg, 0, sizeof(*msg));
    msg->msg_id       = msg_id;
    msg->sender_id    = sender_id;
    msg->receiver_id  = HSYS_MODULE_ID_INVALID;
    msg->priority     = HSYS_PRIORITY_NORMAL;
    msg->payload_size = desc->payload_size;
    msg->desc         = desc;
    msg->ref_count    = 0;  // set to subscriber count at publish time

    if (desc->payload_size > 0) {
        msg->payload = hsys_pool_alloc(desc->payload_size);
        if (!msg->payload) {
            LOG_MSG_ERROR(HSYS_MSG_LOG_EN, "create: pool exhausted for msg_id=0x%04X size=%u",
                   msg_id, desc->payload_size);
            header_free(msg);
            return nullptr;
        }
    }

    return msg;
}

// ---------------------------------------------------------------------------
// Release  (called by dispatch loop after each on_msg_received)
// ---------------------------------------------------------------------------

void hsys_msg_release(hsys_msg_t *msg)
{
    if (!msg) return;

    // Decrement ref count under critical section for atomicity on 8/16-bit MCUs
    hsys_critical_enter();
    uint8_t remaining = (uint8_t)(msg->ref_count - 1u);
    msg->ref_count = remaining;
    hsys_critical_exit();

    if (remaining == 0) {
        if (msg->payload) {
            if (msg->cleanup) {
                msg->cleanup(msg->payload);
            }
            hsys_pool_free(msg->payload);
            msg->payload = nullptr;
        }
        header_free(msg);
    }
}

// ---------------------------------------------------------------------------
// Wake token  (no descriptor, no payload — direct header pool access)
// ---------------------------------------------------------------------------

hsys_msg_t *hsys_msg_create_wake(hsys_module_id_t module_id)
{
    if (!s_initialised) return nullptr;
    hsys_msg_t *msg = header_alloc();
    if (!msg) {
        LOG_MSG_ERROR(HSYS_MSG_LOG_EN, "create_wake: header pool exhausted");
        return nullptr;
    }
    memset(msg, 0, sizeof(*msg));
    msg->msg_id      = HSYS_MSG_ID_WAKE;
    msg->sender_id   = module_id;
    msg->receiver_id = module_id;
    msg->ref_count   = 1;
    msg->payload     = nullptr;
    msg->desc        = nullptr;
    return msg;
}

hsys_msg_t *hsys_msg_create_wake_from_isr(hsys_module_id_t module_id)
{
    // header_alloc uses a mutex which is not ISR-safe, but on FreeRTOS the
    // header pool is small and contention is negligible.  For now we call
    // the same path; replace with a lock-free pool if ISR latency matters.
    return hsys_msg_create_wake(module_id);
}

// ---------------------------------------------------------------------------
// Publish
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_publish(hsys_msg_t *msg)
{
    if (!s_initialised) return HSYS_ERR_NOT_INIT;
    if (!msg)           return HSYS_ERR_NULL;

    hsys_sub_entry_t *e = sub_entry_for(msg->msg_id);
    if (!e) return HSYS_ERR_INVALID;

    msg->timestamp = hsys_task_get_tick_ms();

    // Snapshot subscriber list
    hsys_mutex_lock(s_sub_mutex);
    hsys_module_id_t snapshot[HSYS_MAX_SUBSCRIBERS_PER_MSG];
    uint8_t count = e->count;
    for (uint8_t i = 0; i < count; i++) snapshot[i] = e->subscribers[i];
    hsys_mutex_unlock(s_sub_mutex);

    if (count == 0) {
        // No subscribers — release immediately
        hsys_msg_release(msg);
        return HSYS_OK;
    }

    // De-duplicate: if multiple subscribers share the same task, enqueue
    // into that task only once.  dispatch_for_task delivers to all
    // same-task subscribers on each dequeue, so enqueueing N times into
    // the same task would cause N² deliveries.
    bool skip[HSYS_MAX_SUBSCRIBERS_PER_MSG] = {};
    uint8_t unique_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (skip[i]) continue;
        unique_count++;
        for (uint8_t j = i + 1; j < count; j++) {
            if (!skip[j] && hsys_task_mgr_same_task(snapshot[i], snapshot[j]))
                skip[j] = true;
        }
    }

    // Stamp ref count before any enqueue to avoid race where first subscriber
    // dispatches and releases before we've finished stamping
    hsys_critical_enter();
    msg->ref_count = unique_count;
    hsys_critical_exit();

    hsys_status_t result = HSYS_OK;
    for (uint8_t i = 0; i < count; i++) {
        if (skip[i]) continue;
        msg->receiver_id = snapshot[i];
        // Enqueue the pointer, not a copy of the struct
        hsys_status_t s = hsys_task_mgr_enqueue_ptr(msg, 0);
        if (s != HSYS_OK) {
            // Drop: release the ref we pre-counted for this subscriber
            hsys_msg_release(msg);
            result = s;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Direct send
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_send(hsys_msg_t *msg, uint32_t timeout_ms)
{
    if (!s_initialised) return HSYS_ERR_NOT_INIT;
    if (!msg)           return HSYS_ERR_NULL;
    if (msg->receiver_id == HSYS_MODULE_ID_INVALID) return HSYS_ERR_INVALID;

    msg->timestamp = hsys_task_get_tick_ms();

    hsys_critical_enter();
    msg->ref_count = 1;
    hsys_critical_exit();

    return hsys_task_mgr_enqueue_ptr(msg, timeout_ms);
}

// ---------------------------------------------------------------------------
// ISR-safe publish
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_publish_from_isr(hsys_msg_t *msg,
                                          bool *higher_priority_woken)
{
    if (!s_initialised) return HSYS_ERR_NOT_INIT;
    if (!msg)           return HSYS_ERR_NULL;

    hsys_sub_entry_t *e = sub_entry_for(msg->msg_id);
    if (!e) return HSYS_ERR_INVALID;

    uint8_t count = e->count;
    if (count == 0) { hsys_msg_release(msg); return HSYS_OK; }

    msg->ref_count = count;

    hsys_status_t result = HSYS_OK;
    bool woken = false;

    for (uint8_t i = 0; i < count; i++) {
        msg->receiver_id = e->subscribers[i];
        bool w = false;
        hsys_status_t s = hsys_task_mgr_enqueue_ptr_from_isr(msg, &w);
        if (w) woken = true;
        if (s != HSYS_OK) { hsys_msg_release(msg); result = s; }
    }

    if (higher_priority_woken) *higher_priority_woken = woken;
    return result;
}

// ---------------------------------------------------------------------------
// Subscription query
// ---------------------------------------------------------------------------

bool hsys_msg_is_subscriber(hsys_msg_id_t msg_id, hsys_module_id_t module_id)
{
    hsys_sub_entry_t *e = sub_entry_for(msg_id);
    if (!e) return false;

    hsys_mutex_lock(s_sub_mutex);
    bool found = false;
    for (uint8_t i = 0; i < e->count; i++) {
        if (e->subscribers[i] == module_id) { found = true; break; }
    }
    hsys_mutex_unlock(s_sub_mutex);
    return found;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void hsys_msg_get_header_pool_info(hsys_msg_header_pool_info_t *info)
{
    if (!info) return;

    hsys_mutex_lock(s_header_mutex);
    uint8_t used = 0;
    for (uint8_t i = 0; i < HSYS_MSG_HEADER_POOL_SIZE; i++)
        if (s_header_used[i]) used++;
    hsys_mutex_unlock(s_header_mutex);

    info->total_slots     = HSYS_MSG_HEADER_POOL_SIZE;
    info->used_slots      = used;
    info->free_slots      = (uint8_t)(HSYS_MSG_HEADER_POOL_SIZE - used);
    info->peak_used_slots = s_header_peak;
}
