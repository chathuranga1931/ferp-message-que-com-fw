// hsys_task_mgr.h
//
// Task manager API — creates RTOS tasks and binds modules to them.
//
// Each managed task owns one queue (its inbox).  All modules bound to
// that task share the same queue.  The task runs a dispatch loop that
// reads messages from the queue and calls hsys_module_dispatch() for
// the correct module.

#ifndef HSYS_TASK_MGR_H
#define HSYS_TASK_MGR_H

#include "hsys_types.h"
#include "hsys_config.h"
#include "hsys_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Task descriptor
// ---------------------------------------------------------------------------

/**
 * Maximum number of modules that can be listed in one hsys_task_desc_t.
 * Zero-terminated, so the array needs one extra slot.
 */
#ifndef HSYS_MAX_MODULES_PER_TASK
#define HSYS_MAX_MODULES_PER_TASK   8
#endif

typedef struct {
    const char       *name;          ///< Task name (for RTOS debug)
    uint16_t          stack_depth;   ///< Stack size in words
    uint8_t           priority;      ///< RTOS task priority
    uint16_t          queue_depth;   ///< Inbox queue depth (0 = use default)

    /**
     * Zero-terminated list of module IDs that run inside this task.
     * Example:  { MODULE_A_ID, MODULE_B_ID, 0 }
     * Pass { 0 } (or leave as default zero-initialised) when using
     * hsys_task_mgr_bind() separately.
     */
    hsys_module_id_t  modules[HSYS_MAX_MODULES_PER_TASK + 1];
} hsys_task_desc_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the task manager and create all managed RTOS tasks.
 *
 * Accepts the full task descriptor table.  Internally initialises all
 * shared state (mutex, barrier, event group) and then creates one RTOS
 * task per entry.  Each task's dispatch loop runs the three lifecycle
 * phases automatically before entering its message loop.
 *
 * @param  tasks  Array of task descriptors.
 * @param  count  Number of entries in the array.
 * @return HSYS_OK, or the first error encountered.
 */
hsys_status_t hsys_task_mgr_init(const hsys_task_desc_t *tasks, uint8_t count);

/**
 * @brief  Create a single managed RTOS task.
 *         Use hsys_task_mgr_init() for the normal table-driven path.
 *
 * @param  desc     Task configuration.
 * @return HSYS_OK, HSYS_ERR_NO_MEM, or HSYS_ERR_ALREADY_EXISTS.
 */
hsys_status_t hsys_task_mgr_create(const hsys_task_desc_t *desc);

/**
 * @brief  Bind a module to a task by task name.
 *         From this point, all messages for that module_id are delivered
 *         to the named task's queue.
 *
 * @param  task_name  Task to bind to (must have been created already).
 * @param  module_id  Module to bind.
 * @return HSYS_OK, HSYS_ERR_NOT_FOUND, or HSYS_ERR_NO_MEM.
 */
hsys_status_t hsys_task_mgr_bind(const char *task_name,
                                  hsys_module_id_t module_id);

/**
 * @brief  Enqueue a message pointer into the task queue of the message's
 *         receiver.  Called internally by hsys_msg_publish() / hsys_msg_send().
 *         The queue carries the pointer itself (zero-copy); ref_count must
 *         already be set by the caller before the first enqueue.
 *
 * @param  msg         Message to enqueue (receiver_id must be set).
 * @param  timeout_ms  Max wait time if queue is full (0 = no wait).
 * @return HSYS_OK, HSYS_ERR_QUEUE_FULL, or HSYS_ERR_NOT_FOUND.
 */
hsys_status_t hsys_task_mgr_enqueue_ptr(hsys_msg_t *msg,
                                         uint32_t timeout_ms);

/**
 * @brief  ISR-safe variant of hsys_task_mgr_enqueue_ptr().
 *
 * @param  msg                      Message pointer to enqueue.
 * @param  higher_priority_woken    Set to true if a higher-priority task
 *                                  was unblocked (caller must yield).
 * @return HSYS_OK or HSYS_ERR_QUEUE_FULL.
 */
hsys_status_t hsys_task_mgr_enqueue_ptr_from_isr(hsys_msg_t *msg,
                                                   bool *higher_priority_woken);

#ifdef __cplusplus
}
#endif

#endif // HSYS_TASK_MGR_H
