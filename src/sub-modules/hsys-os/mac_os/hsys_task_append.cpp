// hsys_task_append.cpp — macOS/POSIX implementation

#include "hsys_task_append.h"

#include <chrono>
#include <mutex>

// ---------------------------------------------------------------------------
// Uptime query
// ---------------------------------------------------------------------------

static const auto s_start_time = std::chrono::steady_clock::now();

uint32_t hsys_task_get_tick_ms(void)
{
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - s_start_time).count()
    );
}

// ---------------------------------------------------------------------------
// Critical section — std::recursive_mutex (not ISR-safe, but correct for sim)
// ---------------------------------------------------------------------------

static std::recursive_mutex s_critical_mutex;

void hsys_critical_enter(void)
{
    s_critical_mutex.lock();
}

void hsys_critical_exit(void)
{
    s_critical_mutex.unlock();
}
