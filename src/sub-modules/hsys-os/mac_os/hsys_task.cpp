// hsys_task.cpp — macOS/POSIX implementation using std::thread

#include "hsys_task.h"

#include <thread>
#include <chrono>
#include <cstdlib>

// std::thread is heap-allocated and cast to void* as the opaque handle.

hsys_task_handle_t hsys_task_create(
    void (*task_function)(void*),
    const char* /*task_name*/,
    uint16_t    /*stack_depth*/,
    void*       parameters,
    uint8_t     /*priority*/)
{
    // Detached thread — lifecycle managed by the OS like an RTOS task.
    std::thread *t = new std::thread(task_function, parameters);
    t->detach();
    return static_cast<hsys_task_handle_t>(t);
}

void hsys_task_delete(hsys_task_handle_t task_handle)
{
    // Detached threads cannot be joined; just release the handle object.
    if (task_handle) {
        delete static_cast<std::thread *>(task_handle);
    }
}

void hsys_task_delay(uint32_t delay_ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
}

void hsys_task_start_scheduler(void)
{
    // No scheduler to start on a real OS — the threads are already running.
    // Block the calling thread (app_main) forever so the process stays alive.
    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
}
