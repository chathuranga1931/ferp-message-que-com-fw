// hsys_task_append.h
//
// ADDITIONAL OS abstractions required by the HSYS messaging architecture.
// These are NOT part of the original hsys_task.h — they are kept separate
// so the delta is clearly visible and easy to port to another project.
//
// To port: implement hsys_task_append.cpp for your target RTOS and drop it
// in the equivalent of src/os/free_rtos/.

#ifndef HSYS_TASK_APPEND_H
#define HSYS_TASK_APPEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Uptime / tick query
// ---------------------------------------------------------------------------

/**
 * @brief  Returns the system uptime in milliseconds since scheduler start.
 *         Used by the message bus to timestamp every published message.
 *
 * @return Uptime in milliseconds (wraps at UINT32_MAX ~49 days).
 */
uint32_t hsys_task_get_tick_ms(void);

// ---------------------------------------------------------------------------
// Critical section (ISR-safe)
// ---------------------------------------------------------------------------

/**
 * @brief  Enter a critical section.
 *         Disables interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY.
 *         Used by the pool manager to make alloc/free ISR-safe.
 *         Must be paired with hsys_critical_exit().
 *
 * @note   Do NOT call blocking OS functions inside a critical section.
 */
void hsys_critical_enter(void);

/**
 * @brief  Exit the critical section entered by hsys_critical_enter().
 *         Re-enables interrupts.
 */
void hsys_critical_exit(void);

#ifdef __cplusplus
}
#endif

#endif // HSYS_TASK_APPEND_H
