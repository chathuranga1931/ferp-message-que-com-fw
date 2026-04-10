// hsys_queue.h
#ifndef HSYS_QUEUE_H
#define HSYS_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    void* queueHandle;  // Opaque pointer to the FreeRTOS queue
    uint32_t queueLength;
} hsys_queue_handle_t;

// Initialize the queue
bool hsys_queue_init(hsys_queue_handle_t* queue, uint32_t queueLength, size_t itemSize);

// Delete the queue
void hsys_queue_deinit(hsys_queue_handle_t* queue);

// Send an item to the queue
bool hsys_queue_send(hsys_queue_handle_t* queue, const void* item, uint32_t ticksToWait);

// Receive an item from the queue
bool hsys_queue_receive(hsys_queue_handle_t* queue, void* item, uint32_t ticksToWait);

// Peek at the item at the front of the queue
bool hsys_queue_peek(hsys_queue_handle_t* queue, void* item, uint32_t ticksToWait);

// Get the number of items currently in the queue
uint32_t hsys_queue_size(hsys_queue_handle_t* queue);

// Check if the queue is empty
bool hsys_queue_is_empty(hsys_queue_handle_t* queue);

// Check if the queue is full
bool hsys_queue_is_full(hsys_queue_handle_t* queue);

// Send an item to the queue from ISR context
bool hsys_queue_send_from_isr(hsys_queue_handle_t* queue, const void* item, bool* higherPriorityTaskWoken);

// Receive an item from the queue from ISR context
bool hsys_queue_receive_from_isr(hsys_queue_handle_t* queue, void* item, bool* higherPriorityTaskWoken);

// Yield from ISR if a higher priority task was woken
void hsys_yield_from_isr(void);

#endif // HSYS_QUEUE_H
