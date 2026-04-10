#include "hsys_semaphore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static inline TickType_t to_ticks(uint32_t wait_time_ms)
{
    return (wait_time_ms == 0xFFFFFFFFUL) ? portMAX_DELAY
                                          : pdMS_TO_TICKS(wait_time_ms);
}

// Internal structure for semaphore
struct hsys_semaphore {
    SemaphoreHandle_t handle;
};

extern "C" {

// Create a binary semaphore
hsys_semaphore_handle_t hsys_semaphore_create(bool initialCount) {
    hsys_semaphore_handle_t sem = (hsys_semaphore_handle_t)pvPortMalloc(sizeof(struct hsys_semaphore));
    if (sem == NULL) {
        return NULL;  // Allocation failed
    }

    sem->handle = xSemaphoreCreateBinary();
    if (sem->handle == NULL) {
        vPortFree(sem);
        return NULL;  // Semaphore creation failed
    }

    // If initial count requested, give the semaphore
    if (initialCount) {
        xSemaphoreGive(sem->handle);
    }

    return sem;
}

// Delete/Destroy semaphore
void hsys_semaphore_delete(hsys_semaphore_handle_t handle) {
    if (handle != NULL) {
        if (handle->handle != NULL) {
            vSemaphoreDelete(handle->handle);
        }
        vPortFree(handle);
    }
}

// Wait (take) the semaphore indefinitely
void hsys_semaphore_take(hsys_semaphore_handle_t handle) {
    if (handle != NULL && handle->handle != NULL) {
        xSemaphoreTake(handle->handle, portMAX_DELAY);
    }
}

// Wait (take) the semaphore with timeout (returns true if taken)
bool hsys_semaphore_take_timeout(hsys_semaphore_handle_t handle, uint32_t timeoutMs) {
    if (handle != NULL && handle->handle != NULL) {
        return xSemaphoreTake(handle->handle, to_ticks(timeoutMs)) == pdTRUE;
    }
    return false;
}

// Give (release) the semaphore
void hsys_semaphore_give(hsys_semaphore_handle_t handle) {
    if (handle != NULL && handle->handle != NULL) {
        xSemaphoreGive(handle->handle);
    }
}

} // extern "C"
