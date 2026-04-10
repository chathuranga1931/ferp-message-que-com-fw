#ifndef HSYS_SEMAPHORE_H
#define HSYS_SEMAPHORE_H

#include <stdint.h>
#include <stdbool.h>

// Opaque handler for the semaphore
typedef struct hsys_semaphore* hsys_semaphore_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

// Create a binary semaphore (initially available or empty based on initialCount)
// Returns NULL on failure
hsys_semaphore_handle_t hsys_semaphore_create(bool initialCount);

// Delete/Destroy semaphore and free resources
void hsys_semaphore_delete(hsys_semaphore_handle_t handle);

// Wait (take) the semaphore (blocking, waits indefinitely)
void hsys_semaphore_take(hsys_semaphore_handle_t handle);

// Wait (take) the semaphore with timeout (returns true if taken, false if timeout)
bool hsys_semaphore_take_timeout(hsys_semaphore_handle_t handle, uint32_t timeoutMs);

// Give (release) the semaphore
void hsys_semaphore_give(hsys_semaphore_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // HSYS_SEMAPHORE_H
