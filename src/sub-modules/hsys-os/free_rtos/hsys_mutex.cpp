// hsys_mutex.cpp
#include "hsys_mutex.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static inline TickType_t to_ticks(uint32_t wait_time_ms)
{
    return (wait_time_ms == 0xFFFFFFFFUL) ? portMAX_DELAY
                                          : pdMS_TO_TICKS(wait_time_ms);
}

// Create a mutex
hsys_mutex_handle_t hsys_mutex_create(void) {
    SemaphoreHandle_t mutex_handle = xSemaphoreCreateMutex();
    return (hsys_mutex_handle_t)mutex_handle;
}

// Delete a mutex
void hsys_mutex_delete(hsys_mutex_handle_t mutex_handle) {
    if (mutex_handle != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)mutex_handle);
    }
}

// Lock a mutex (wait indefinitely)
void hsys_mutex_lock(hsys_mutex_handle_t mutex_handle) {
    if (mutex_handle != NULL) {
        xSemaphoreTake((SemaphoreHandle_t)mutex_handle, portMAX_DELAY);
    }
}

// Unlock a mutex
void hsys_mutex_unlock(hsys_mutex_handle_t mutex_handle) {
    if (mutex_handle != NULL) {
        xSemaphoreGive((SemaphoreHandle_t)mutex_handle);
    }
}

// Try to lock a mutex (non-blocking or with timeout)
uint8_t hsys_mutex_try_lock(hsys_mutex_handle_t mutex_handle, uint32_t wait_time_ms) {
    if (mutex_handle != NULL) {
        return xSemaphoreTake((SemaphoreHandle_t)mutex_handle, to_ticks(wait_time_ms)) == pdTRUE;
    }
    return 0; // Failed
}
