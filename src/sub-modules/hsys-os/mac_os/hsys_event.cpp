// hsys_event.cpp — macOS implementation using atomic<uint32_t> + condition_variable

#include "hsys_event.h"

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <new>
#include <cstdint>

struct MacOsEventGroup {
    std::atomic<uint32_t>   bits;
    std::mutex              mtx;
    std::condition_variable cv;
};

hsys_eventgroup_handle_t hsys_event_group_create(void)
{
    MacOsEventGroup *eg = new (std::nothrow) MacOsEventGroup();
    if (!eg) return nullptr;
    eg->bits.store(0, std::memory_order_relaxed);
    return static_cast<hsys_eventgroup_handle_t>(eg);
}

void hsys_event_group_delete(hsys_eventgroup_handle_t event_group_handle)
{
    if (event_group_handle) {
        delete static_cast<MacOsEventGroup *>(event_group_handle);
    }
}

void hsys_event_group_set_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_set)
{
    if (!event_group_handle) return;
    MacOsEventGroup *eg = static_cast<MacOsEventGroup *>(event_group_handle);
    eg->bits.fetch_or(bits_to_set, std::memory_order_release);
    eg->cv.notify_all();
}

void hsys_event_group_clear_bits(hsys_eventgroup_handle_t event_group_handle, uint32_t bits_to_clear)
{
    if (!event_group_handle) return;
    MacOsEventGroup *eg = static_cast<MacOsEventGroup *>(event_group_handle);
    eg->bits.fetch_and(~bits_to_clear, std::memory_order_release);
}

uint32_t hsys_event_group_wait_bits(hsys_eventgroup_handle_t event_group_handle,
                                     uint32_t bits_to_wait_for,
                                     uint8_t  clear_on_exit,
                                     uint8_t  wait_for_all_bits,
                                     uint32_t wait_time_ms)
{
    if (!event_group_handle) return 0;
    MacOsEventGroup *eg = static_cast<MacOsEventGroup *>(event_group_handle);

    std::unique_lock<std::mutex> lock(eg->mtx);

    auto condition_met = [&]() -> bool {
        uint32_t current = eg->bits.load(std::memory_order_acquire);
        if (wait_for_all_bits) {
            return (current & bits_to_wait_for) == bits_to_wait_for;
        } else {
            return (current & bits_to_wait_for) != 0;
        }
    };

    bool satisfied = false;
    if (wait_time_ms == 0xFFFFFFFFUL) {
        eg->cv.wait(lock, condition_met);
        satisfied = true;
    } else {
        satisfied = eg->cv.wait_for(lock,
                        std::chrono::milliseconds(wait_time_ms),
                        condition_met);
    }

    uint32_t result = eg->bits.load(std::memory_order_acquire);

    if (satisfied && clear_on_exit) {
        eg->bits.fetch_and(~bits_to_wait_for, std::memory_order_release);
    }

    return result;
}
