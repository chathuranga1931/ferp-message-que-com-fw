// hsys_task.h
#ifndef HSYS_TASK_H
#define HSYS_TASK_H

#include <stdint.h>
#include <stddef.h>

// Opaque handle for tasks
typedef void* hsys_task_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

// Function to create a task
hsys_task_handle_t hsys_task_create(
    void (*task_function)(void*), 
    const char* task_name, 
    uint16_t stack_depth, 
    void* parameters, 
    uint8_t priority);

// Function to delete a task
void hsys_task_delete(hsys_task_handle_t task_handle);

// Function to delay a task for a specified number of milliseconds
void hsys_task_delay(uint32_t delay_ms);

// Function to start the scheduler
void hsys_task_start_scheduler(void);

#ifdef __cplusplus
}
#endif

#endif // HSYS_TASK_H