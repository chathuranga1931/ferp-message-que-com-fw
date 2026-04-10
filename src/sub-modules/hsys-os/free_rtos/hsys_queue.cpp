// hsys_queue.cpp
#include "hsys_queue.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>

// Map HSYS_WAIT_FOREVER (0xFFFFFFFF) to portMAX_DELAY; otherwise convert ms.
static inline TickType_t to_ticks(uint32_t wait_time_ms)
{
    return (wait_time_ms == 0xFFFFFFFFUL) ? portMAX_DELAY
                                          : pdMS_TO_TICKS(wait_time_ms);
}

bool hsys_queue_init(hsys_queue_handle_t* queue, uint32_t queueLength, size_t itemSize) {
    if (!queue) return false;
    queue->queueHandle = xQueueCreate(queueLength, itemSize);
    if (!queue->queueHandle) return false;
    queue->queueLength = queueLength;
    return true;
}

void hsys_queue_deinit(hsys_queue_handle_t* queue) {
    if (queue && queue->queueHandle) {
        vQueueDelete((QueueHandle_t)queue->queueHandle);
        queue->queueHandle = NULL;
    }
}

bool hsys_queue_send(hsys_queue_handle_t* queue, const void* item, uint32_t ticksToWaitms) {
    if (!queue || !queue->queueHandle || !item) return false;
    return xQueueSend((QueueHandle_t)queue->queueHandle, item, to_ticks(ticksToWaitms)) == pdPASS;
}

bool hsys_queue_receive(hsys_queue_handle_t* queue, void* item, uint32_t ticksToWaitms) {
    if (!queue || !queue->queueHandle || !item) return false;
    return xQueueReceive((QueueHandle_t)queue->queueHandle, item, to_ticks(ticksToWaitms)) == pdPASS;
}

bool hsys_queue_peek(hsys_queue_handle_t* queue, void* item, uint32_t ticksToWaitms) {
    if (!queue || !queue->queueHandle || !item) return false;
    return xQueuePeek((QueueHandle_t)queue->queueHandle, item, to_ticks(ticksToWaitms)) == pdPASS;
}

uint32_t hsys_queue_size(hsys_queue_handle_t* queue) {
    if (!queue || !queue->queueHandle) return 0;
    return uxQueueMessagesWaiting((QueueHandle_t)queue->queueHandle);
}

bool hsys_queue_is_empty(hsys_queue_handle_t* queue) {
    return hsys_queue_size(queue) == 0;
}

bool hsys_queue_is_full(hsys_queue_handle_t* queue) {
    if (!queue || !queue->queueHandle) return false;
    return hsys_queue_size(queue) >= queue->queueLength;
}

bool hsys_queue_send_from_isr(hsys_queue_handle_t* queue, const void* item, bool* higherPriorityTaskWoken) {
    if (!queue || !queue->queueHandle || !item) return false;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueSendFromISR((QueueHandle_t)queue->queueHandle, item, &xHigherPriorityTaskWoken);
    
    if (higherPriorityTaskWoken) {
        *higherPriorityTaskWoken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS);
}

bool hsys_queue_receive_from_isr(hsys_queue_handle_t* queue, void* item, bool* higherPriorityTaskWoken) {
    if (!queue || !queue->queueHandle || !item) return false;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueReceiveFromISR((QueueHandle_t)queue->queueHandle, item, &xHigherPriorityTaskWoken);
    
    if (higherPriorityTaskWoken) {
        *higherPriorityTaskWoken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS);
}

void hsys_yield_from_isr(void) {
    portYIELD_FROM_ISR();
}
