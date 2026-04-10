#include "hsys_task.h"

extern "C" {
	#include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
}

// Create a task and return its handle
hsys_task_handle_t hsys_task_create(
    void (*task_function)(void*), 
    const char* task_name, 
    uint16_t stack_depth, 
    void* parameters, 
    uint8_t priority) {
    TaskHandle_t task_handle = NULL;
    BaseType_t result = xTaskCreate(
        task_function,   // Task function
        task_name,       // Name of the task
        stack_depth,     // Stack depth in words
        parameters,      // Parameters to the task function
        priority,        // Task priority
        &task_handle     // Handle to the created task
    );

    return (result == pdPASS) ? (hsys_task_handle_t)task_handle : NULL;
}

// Delete a task
void hsys_task_delete(hsys_task_handle_t task_handle) {
    if (task_handle != NULL) {
        vTaskDelete((TaskHandle_t)task_handle);
    }
}

// Delay a task for a specified number of milliseconds
void hsys_task_delay(uint32_t delay_ms) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

// Start the scheduler
void hsys_task_start_scheduler(void) {
    vTaskStartScheduler();
}
