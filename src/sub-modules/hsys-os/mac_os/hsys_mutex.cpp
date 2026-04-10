// hsys_mutex.cpp — macOS/POSIX implementation using std::recursive_mutex
// recursive_mutex matches FreeRTOS mutex re-take behaviour.

#include "hsys_mutex.h"

#include <mutex>
#include <chrono>
#include <new>

hsys_mutex_handle_t hsys_mutex_create(void)
{
    return static_cast<hsys_mutex_handle_t>(new std::recursive_timed_mutex());
}

void hsys_mutex_delete(hsys_mutex_handle_t mutex_handle)
{
    if (mutex_handle) {
        delete static_cast<std::recursive_timed_mutex *>(mutex_handle);
    }
}

void hsys_mutex_lock(hsys_mutex_handle_t mutex_handle)
{
    if (mutex_handle) {
        static_cast<std::recursive_timed_mutex *>(mutex_handle)->lock();
    }
}

void hsys_mutex_unlock(hsys_mutex_handle_t mutex_handle)
{
    if (mutex_handle) {
        static_cast<std::recursive_timed_mutex *>(mutex_handle)->unlock();
    }
}

uint8_t hsys_mutex_try_lock(hsys_mutex_handle_t mutex_handle, uint32_t wait_time_ms)
{
    if (!mutex_handle) return 0;
    auto *m = static_cast<std::recursive_timed_mutex *>(mutex_handle);
    if (wait_time_ms == 0xFFFFFFFFUL) {
        m->lock();
        return 1;
    }
    return m->try_lock_for(std::chrono::milliseconds(wait_time_ms)) ? 1 : 0;
}
