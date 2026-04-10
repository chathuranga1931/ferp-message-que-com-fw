// hsys_mutex.h
#ifndef HSYS_MUTEX_H
#define HSYS_MUTEX_H

#include <stdint.h>

// Opaque handle for mutexes
typedef void* hsys_mutex_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

// Create a mutex
hsys_mutex_handle_t hsys_mutex_create(void);

// Delete a mutex
void hsys_mutex_delete(hsys_mutex_handle_t mutex_handle);

// Lock a mutex (wait indefinitely)
void hsys_mutex_lock(hsys_mutex_handle_t mutex_handle);

// Unlock a mutex
void hsys_mutex_unlock(hsys_mutex_handle_t mutex_handle);

// Try to lock a mutex (non-blocking)
uint8_t hsys_mutex_try_lock(hsys_mutex_handle_t mutex_handle, uint32_t wait_time_ms);

#ifdef __cplusplus
}
#endif

#endif // HSYS_MUTEX_H