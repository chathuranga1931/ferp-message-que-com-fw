
// hsys_event.cpp
#include "hsys_event.h"

extern "C" {
	#include "freertos/FreeRTOS.h"
    #include "freertos/event_groups.h"
}

// Map HSYS_WAIT_FOREVER (0xFFFFFFFF) to portMAX_DELAY; otherwise convert ms.
static inline TickType_t to_ticks(uint32_t wait_time_ms)
{
    return (wait_time_ms == 0xFFFFFFFFUL) ? portMAX_DELAY
                                          : pdMS_TO_TICKS(wait_time_ms);
}

// Create an event group and return its handle
hsys_eventgroup_handle_t hsys_event_group_create(void) {
    EventGroupHandle_t event_group_handle = xEventGroupCreate();
    return (hsys_eventgroup_handle_t)event_group_handle;
}

// Delete an event group
void hsys_event_group_delete(hsys_eventgroup_handle_t event_group_handle) {
    if (event_group_handle != NULL) {
        vEventGroupDelete((EventGroupHandle_t)event_group_handle);
    }
}

// Set event bits
void hsys_event_group_set_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_set) {
    if (event_group_handle != NULL) {
        xEventGroupSetBits((EventGroupHandle_t)event_group_handle, bits_to_set);
    }
}

// Clear event bits
void hsys_event_group_clear_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_clear) {
    if (event_group_handle != NULL) {
        xEventGroupClearBits((EventGroupHandle_t)event_group_handle, bits_to_clear);
    }
}

// Wait for event bits
uint32_t hsys_event_group_wait_bits(
    hsys_eventgroup_handle_t event_group_handle,
    uint32_t bits_to_wait_for,
    uint8_t clear_on_exit,
    uint8_t wait_for_all,
    uint32_t wait_time_ms) {
    if (event_group_handle != NULL) {
        return xEventGroupWaitBits(
            (EventGroupHandle_t)event_group_handle,
            bits_to_wait_for,
            clear_on_exit,
            wait_for_all,
            to_ticks(wait_time_ms)
        );
    }
    return 0;
}
