// hsys_semaphore.cpp — macOS implementation using std::mutex + condition_variable

#include "hsys_semaphore.h"

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <new>

struct hsys_semaphore {
    std::mutex              mtx;
    std::condition_variable cv;
    bool                    available;
};

hsys_semaphore_handle_t hsys_semaphore_create(bool initial_count)
{
    hsys_semaphore *s = new (std::nothrow) hsys_semaphore();
    if (!s) return nullptr;
    s->available = initial_count;
    return s;
}

void hsys_semaphore_delete(hsys_semaphore_handle_t semaphore_handle)
{
    if (semaphore_handle) {
        delete semaphore_handle;
    }
}

void hsys_semaphore_take(hsys_semaphore_handle_t semaphore_handle)
{
    if (!semaphore_handle) return;
    std::unique_lock<std::mutex> lock(semaphore_handle->mtx);
    semaphore_handle->cv.wait(lock, [&]() { return semaphore_handle->available; });
    semaphore_handle->available = false;
}

bool hsys_semaphore_take_timeout(hsys_semaphore_handle_t semaphore_handle, uint32_t wait_time_ms)
{
    if (!semaphore_handle) return false;
    std::unique_lock<std::mutex> lock(semaphore_handle->mtx);

    if (wait_time_ms == 0xFFFFFFFFUL) {
        semaphore_handle->cv.wait(lock, [&]() { return semaphore_handle->available; });
        semaphore_handle->available = false;
        return true;
    }

    bool ok = semaphore_handle->cv.wait_for(lock,
                  std::chrono::milliseconds(wait_time_ms),
                  [&]() { return semaphore_handle->available; });
    if (ok) semaphore_handle->available = false;
    return ok;
}

void hsys_semaphore_give(hsys_semaphore_handle_t semaphore_handle)
{
    if (!semaphore_handle) return;
    {
        std::lock_guard<std::mutex> lock(semaphore_handle->mtx);
        semaphore_handle->available = true;
    }
    semaphore_handle->cv.notify_one();
}
