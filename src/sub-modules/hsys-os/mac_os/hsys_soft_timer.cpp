// hsys_soft_timer.cpp — macOS implementation using std::thread

#include "hsys_soft_timer.h"

#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <new>
#include <cstdint>
#include <cstdio>

using timer_cb_t = void (*)(void *);

struct MacOsTimer {
    std::string       name;
    uint32_t          period_ms;
    bool              auto_reload;
    void             *user_data;
    timer_cb_t        callback;
    std::thread       thread;
    std::atomic<bool> running;

    MacOsTimer()
        : period_ms(0), auto_reload(false), user_data(nullptr),
          callback(nullptr), running(false)
    {}
};

hsys_timer_handle_t hsys_timer_create(const char *name,
                                       uint32_t    period_ms,
                                       bool        auto_reload,
                                       void       *user_data,
                                       void      (*callback)(void *))
{
    MacOsTimer *t = new (std::nothrow) MacOsTimer();
    if (!t) {
        fprintf(stderr, "[hsys_timer_create] new MacOsTimer() returned nullptr for '%s'\n", name ? name : "?");
        return nullptr;
    }
    t->name        = name ? name : "";
    t->period_ms   = period_ms;
    t->auto_reload = auto_reload;
    t->user_data   = user_data;
    t->callback    = callback;
    return static_cast<hsys_timer_handle_t>(t);
}

void hsys_timer_delete(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return;
    MacOsTimer *t = static_cast<MacOsTimer *>(timer_handle);
    hsys_stop_timer(timer_handle);
    if (t->thread.joinable()) t->thread.join();
    delete t;
}

bool hsys_start_timer(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return false;
    MacOsTimer *t = static_cast<MacOsTimer *>(timer_handle);

    // Prevent double-start
    bool expected = false;
    if (!t->running.compare_exchange_strong(expected, true)) return false;

    t->thread = std::thread([t]() {
        do {
            auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(t->period_ms);
            // Sleep in small slices so we can observe running flag
            while (t->running.load() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!t->running.load()) break;
            if (t->callback) t->callback(static_cast<void*>(t));  // pass handle, like FreeRTOS
        } while (t->auto_reload && t->running.load());

        t->running.store(false);
    });
    t->thread.detach();
    return true;
}

bool hsys_stop_timer(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return false;
    MacOsTimer *t = static_cast<MacOsTimer *>(timer_handle);
    t->running.store(false);
    return true;
}

bool hsys_reset_timer(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return false;
    hsys_stop_timer(timer_handle);
    // Brief wait so the detached thread can observe the stop
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return hsys_start_timer(timer_handle);
}

bool hsys_delete_timer(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return false;
    MacOsTimer *t = static_cast<MacOsTimer *>(timer_handle);
    t->running.store(false);
    // Give the detached thread a moment to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    delete t;
    return true;
}

void *hsys_timer_get_userdata(hsys_timer_handle_t timer_handle)
{
    if (!timer_handle) return nullptr;
    return static_cast<MacOsTimer *>(timer_handle)->user_data;
}

bool hsys_soft_timer_set_period(hsys_timer_handle_t timer_handle,
                                 uint32_t            period_ms,
                                 uint32_t            /* ticks_to_wait */)
{
    if (!timer_handle) return false;
    MacOsTimer *t = static_cast<MacOsTimer *>(timer_handle);
    t->period_ms = period_ms;
    return true;
}
