// hsys_event.h
#ifndef HSYS_EVENT_H
#define HSYS_EVENT_H

#include <stdint.h>
#include <stddef.h>

// Opaque handle for event groups
typedef void* hsys_eventgroup_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

// Function to create an event group
hsys_eventgroup_handle_t hsys_event_group_create(void);

// Function to delete an event group
void hsys_event_group_delete(hsys_eventgroup_handle_t event_group_handle);

// Function to set event bits
void hsys_event_group_set_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_set);

// Function to clear event bits
void hsys_event_group_clear_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_clear);

// Function to wait for event bits
uint32_t hsys_event_group_wait_bits(
    hsys_eventgroup_handle_t event_group_handle,
    uint32_t bits_to_wait_for,
    uint8_t clear_on_exit,
    uint8_t wait_for_all,
    uint32_t wait_time_ms);

#ifdef __cplusplus
}
#endif

#endif // HSYS_EVENT_H
