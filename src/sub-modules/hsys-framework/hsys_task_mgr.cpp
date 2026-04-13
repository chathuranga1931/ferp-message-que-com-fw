// hsys_task_mgr.cpp
//
// Task manager implementation.
//
// Each managed task runs this startup sequence before its message loop:
//
//   1. pre_init()  for each bound module
//      ── barrier: wait for ALL tasks to finish pre_init ──
//   2. init()      for each bound module
//      ── barrier: wait for ALL tasks to finish init ──
//   3. post_init() for each bound module
//      ── barrier: wait for ALL tasks to finish post_init ──
//   4. Enter the message dispatch loop
//
// The barrier uses a shared atomic counter (one per phase) and an
// event-group bit.  The last task to finish a phase sets the bit;
// all tasks block on that bit before moving to the next phase.

#include "hsys_task_mgr.h"
#include "hsys_module.h"
#include "hsys_task.h"
#include "hsys_queue.h"
#include "hsys_mutex.h"
#include "hsys_event.h"
#include "hsys_log.h"

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

#define TASK_NAME_MAX_LEN  16

typedef struct {
    char                  name[TASK_NAME_MAX_LEN];
    hsys_task_handle_t    task_handle;
    hsys_queue_handle_t   queue;
    hsys_module_id_t      bound_modules[HSYS_MAX_MODULES];
    uint8_t               module_count;
} hsys_task_entry_t;

// ---------------------------------------------------------------------------
// Barrier state  (shared across all tasks)
// ---------------------------------------------------------------------------

#define BIT_PRE_INIT_DONE   (1UL << 0)
#define BIT_INIT_DONE       (1UL << 1)
#define BIT_POST_INIT_DONE  (1UL << 2)

static hsys_eventgroup_handle_t s_lifecycle_eg   = nullptr;
static volatile uint8_t         s_pre_init_done  = 0;
static volatile uint8_t         s_init_done      = 0;
static volatile uint8_t         s_post_init_done = 0;
static hsys_mutex_handle_t      s_barrier_mutex  = nullptr;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static hsys_task_entry_t   s_tasks[HSYS_MAX_TASKS];
static uint8_t             s_task_count  = 0;
static hsys_mutex_handle_t s_mutex       = nullptr;
static bool                s_initialised = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static hsys_task_entry_t *find_task_by_name(const char *name)
{
    for (uint8_t i = 0; i < s_task_count; i++) {
        if (strncmp(s_tasks[i].name, name, TASK_NAME_MAX_LEN) == 0)
            return &s_tasks[i];
    }
    return nullptr;
}

static hsys_task_entry_t *find_task_for_module(hsys_module_id_t module_id)
{
    for (uint8_t i = 0; i < s_task_count; i++)
        for (uint8_t j = 0; j < s_tasks[i].module_count; j++)
            if (s_tasks[i].bound_modules[j] == module_id)
                return &s_tasks[i];
    return nullptr;
}

// Increment a phase counter; if this task was the last, set the event bit.
static void barrier_complete_phase(volatile uint8_t *counter,
                                   uint32_t          done_bit)
{
    hsys_mutex_lock(s_barrier_mutex);
    uint8_t val = (uint8_t)(*counter + 1u);
    *counter = val;
    hsys_mutex_unlock(s_barrier_mutex);

    if (val >= s_task_count)
        hsys_event_group_set_bits(s_lifecycle_eg, done_bit);
}

static void barrier_wait_phase(uint32_t done_bit)
{
    hsys_event_group_wait_bits(
        s_lifecycle_eg,
        done_bit,
        0,                  // do NOT clear on exit (all tasks need to see it)
        1,                  // wait_all = true (only one bit here)
        HSYS_WAIT_FOREVER
    );
}

// ---------------------------------------------------------------------------
// Dispatch loop — runs inside each managed RTOS task
// ---------------------------------------------------------------------------

static void dispatch_loop(void *param)
{
    hsys_task_entry_t *entry  = (hsys_task_entry_t *)param;
    const hsys_module_id_t *mids   = entry->bound_modules;
    const uint8_t           mcount = entry->module_count;

    // ── Phase 1: pre_init ────────────────────────────────────────────────
    hsys_module_pre_init_for_task(mids, mcount);
    barrier_complete_phase(&s_pre_init_done, BIT_PRE_INIT_DONE);
    barrier_wait_phase(BIT_PRE_INIT_DONE);

    // ── Phase 2: init ────────────────────────────────────────────────────
    hsys_module_init_for_task(mids, mcount);
    barrier_complete_phase(&s_init_done, BIT_INIT_DONE);
    barrier_wait_phase(BIT_INIT_DONE);

    // ── Phase 3: post_init ───────────────────────────────────────────────
    hsys_module_post_init_for_task(mids, mcount);
    barrier_complete_phase(&s_post_init_done, BIT_POST_INIT_DONE);
    barrier_wait_phase(BIT_POST_INIT_DONE);

    hsys_log("[%s] lifecycle complete \xe2\x80\x94 entering message loop\n", entry->name);

    // ── Message loop ────────────────────────────────────────────────────────
    // Queue items are hsys_msg_t* pointers (zero-copy delivery).
    // hsys_msg_release() is called after dispatch to decrement ref_count.
    hsys_msg_t *msg_ptr;
    for (;;) {
        if (hsys_queue_receive(&entry->queue, &msg_ptr, HSYS_WAIT_FOREVER)) {
            hsys_module_dispatch_for_task(msg_ptr, mids, mcount);
            hsys_msg_release(msg_ptr);
        }
    }
}

// ---------------------------------------------------------------------------
// API implementation
// ---------------------------------------------------------------------------

hsys_status_t hsys_task_mgr_init(const hsys_task_desc_t *tasks, uint8_t count)
{
    if (s_initialised) return HSYS_OK;
    if (tasks == nullptr || count == 0) return HSYS_ERR_NULL;

    memset(s_tasks, 0, sizeof(s_tasks));
    s_task_count     = 0;
    s_pre_init_done  = 0;
    s_init_done      = 0;
    s_post_init_done = 0;

    s_mutex         = hsys_mutex_create();
    s_barrier_mutex = hsys_mutex_create();
    s_lifecycle_eg  = hsys_event_group_create();

    if (!s_mutex || !s_barrier_mutex || !s_lifecycle_eg) return HSYS_ERR_NO_MEM;

    s_initialised = true;

    for (uint8_t i = 0; i < count; i++) {
        hsys_status_t st = hsys_task_mgr_create(&tasks[i]);
        if (st != HSYS_OK) return st;
    }
    return HSYS_OK;
}

hsys_status_t hsys_task_mgr_create(const hsys_task_desc_t *desc)
{
    if (!s_initialised)  return HSYS_ERR_NOT_INIT;
    if (desc == nullptr) return HSYS_ERR_NULL;

    hsys_mutex_lock(s_mutex);

    if (find_task_by_name(desc->name) != nullptr) {
        hsys_mutex_unlock(s_mutex);
        return HSYS_ERR_ALREADY_EXISTS;
    }
    if (s_task_count >= HSYS_MAX_TASKS) {
        hsys_mutex_unlock(s_mutex);
        return HSYS_ERR_NO_MEM;
    }

    hsys_task_entry_t *entry = &s_tasks[s_task_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, desc->name, TASK_NAME_MAX_LEN - 1);

    uint16_t qdepth = desc->queue_depth > 0
                    ? desc->queue_depth
                    : HSYS_DEFAULT_QUEUE_DEPTH;

    if (!hsys_queue_init(&entry->queue, qdepth, sizeof(hsys_msg_t *))) {
        hsys_mutex_unlock(s_mutex);
        return HSYS_ERR_NO_MEM;
    }

    // Bind modules listed inline in the descriptor (zero-terminated).
    for (int i = 0; i < HSYS_MAX_MODULES_PER_TASK; i++) {
        hsys_module_id_t mid = desc->modules[i];
        if (mid == 0) break;
        if (entry->module_count < HSYS_MAX_MODULES)
            entry->bound_modules[entry->module_count++] = mid;
    }

    // Increment BEFORE creating the task so the barrier denominator is correct
    // if the task starts and calls barrier_complete_phase immediately.
    s_task_count++;

    entry->task_handle = hsys_task_create(
        dispatch_loop, entry->name,
        desc->stack_depth, (void *)entry, desc->priority
    );

    if (entry->task_handle == nullptr) {
        s_task_count--;
        hsys_queue_deinit(&entry->queue);
        hsys_mutex_unlock(s_mutex);
        return HSYS_ERR_NO_MEM;
    }

    hsys_mutex_unlock(s_mutex);
    return HSYS_OK;
}

hsys_status_t hsys_task_mgr_bind(const char *task_name,
                                  hsys_module_id_t module_id)
{
    if (!s_initialised || task_name == nullptr) return HSYS_ERR_NOT_INIT;

    hsys_mutex_lock(s_mutex);
    hsys_task_entry_t *entry = find_task_by_name(task_name);
    if (!entry) { hsys_mutex_unlock(s_mutex); return HSYS_ERR_NOT_FOUND; }
    if (entry->module_count >= HSYS_MAX_MODULES) { hsys_mutex_unlock(s_mutex); return HSYS_ERR_NO_MEM; }
    entry->bound_modules[entry->module_count++] = module_id;
    hsys_mutex_unlock(s_mutex);
    return HSYS_OK;
}

hsys_status_t hsys_task_mgr_enqueue_ptr(hsys_msg_t *msg, uint32_t timeout_ms)
{
    if (!s_initialised || msg == nullptr) return HSYS_ERR_NOT_INIT;
    hsys_task_entry_t *entry = find_task_for_module(msg->receiver_id);
    if (!entry) return HSYS_ERR_NOT_FOUND;
    // Enqueue the pointer itself
    return hsys_queue_send(&entry->queue, &msg, timeout_ms) ? HSYS_OK : HSYS_ERR_QUEUE_FULL;
}

hsys_status_t hsys_task_mgr_enqueue_ptr_from_isr(hsys_msg_t *msg,
                                                   bool *higher_priority_woken)
{
    if (!s_initialised || msg == nullptr) return HSYS_ERR_NOT_INIT;
    hsys_task_entry_t *entry = find_task_for_module(msg->receiver_id);
    if (!entry) return HSYS_ERR_NOT_FOUND;
    bool woken = false;
    bool ok = hsys_queue_send_from_isr(&entry->queue, &msg, &woken);
    if (higher_priority_woken) *higher_priority_woken = woken;
    return ok ? HSYS_OK : HSYS_ERR_QUEUE_FULL;
}
