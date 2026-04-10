// hsys_queue.cpp — macOS implementation using std::queue + condition_variable

#include "hsys_queue.h"

#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <chrono>
#include <new>

struct MacOsQueue {
    std::queue<std::vector<uint8_t>> items;
    std::mutex                       mtx;
    std::condition_variable          cv_not_empty;
    std::condition_variable          cv_not_full;
    uint32_t                         max_length;
    size_t                           item_size;
};

bool hsys_queue_init(hsys_queue_handle_t *queue_handle, uint32_t length, size_t item_size)
{
    if (!queue_handle || length == 0 || item_size == 0) return false;

    MacOsQueue *q = new (std::nothrow) MacOsQueue();
    if (!q) return false;

    q->max_length = length;
    q->item_size  = item_size;

    queue_handle->queueHandle = static_cast<void *>(q);
    queue_handle->queueLength = length;
    return true;
}

void hsys_queue_deinit(hsys_queue_handle_t *queue_handle)
{
    if (!queue_handle || !queue_handle->queueHandle) return;
    delete static_cast<MacOsQueue *>(queue_handle->queueHandle);
    queue_handle->queueHandle = nullptr;
    queue_handle->queueLength = 0;
}

bool hsys_queue_send(hsys_queue_handle_t *queue_handle, const void *item, uint32_t wait_time_ms)
{
    if (!queue_handle || !queue_handle->queueHandle || !item) return false;
    MacOsQueue *q = static_cast<MacOsQueue *>(queue_handle->queueHandle);

    std::unique_lock<std::mutex> lock(q->mtx);

    auto space_available = [&]() { return q->items.size() < q->max_length; };

    if (wait_time_ms == 0xFFFFFFFFUL) {
        q->cv_not_full.wait(lock, space_available);
    } else {
        bool ok = q->cv_not_full.wait_for(lock,
                      std::chrono::milliseconds(wait_time_ms),
                      space_available);
        if (!ok) return false;
    }

    std::vector<uint8_t> buf(q->item_size);
    std::memcpy(buf.data(), item, q->item_size);
    q->items.push(std::move(buf));
    q->cv_not_empty.notify_one();
    return true;
}

bool hsys_queue_receive(hsys_queue_handle_t *queue_handle, void *item, uint32_t wait_time_ms)
{
    if (!queue_handle || !queue_handle->queueHandle || !item) return false;
    MacOsQueue *q = static_cast<MacOsQueue *>(queue_handle->queueHandle);

    std::unique_lock<std::mutex> lock(q->mtx);

    auto item_available = [&]() { return !q->items.empty(); };

    if (wait_time_ms == 0xFFFFFFFFUL) {
        q->cv_not_empty.wait(lock, item_available);
    } else {
        bool ok = q->cv_not_empty.wait_for(lock,
                      std::chrono::milliseconds(wait_time_ms),
                      item_available);
        if (!ok) return false;
    }

    std::memcpy(item, q->items.front().data(), q->item_size);
    q->items.pop();
    q->cv_not_full.notify_one();
    return true;
}

bool hsys_queue_send_from_isr(hsys_queue_handle_t *queue_handle, const void *item,
                               bool *higher_priority_task_woken)
{
    if (higher_priority_task_woken) *higher_priority_task_woken = false;
    if (!queue_handle || !queue_handle->queueHandle || !item) return false;
    MacOsQueue *q = static_cast<MacOsQueue *>(queue_handle->queueHandle);

    std::unique_lock<std::mutex> lock(q->mtx, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (q->items.size() >= q->max_length) return false;

    std::vector<uint8_t> buf(q->item_size);
    std::memcpy(buf.data(), item, q->item_size);
    q->items.push(std::move(buf));
    q->cv_not_empty.notify_one();
    return true;
}

bool hsys_queue_receive_from_isr(hsys_queue_handle_t *queue_handle, void *item,
                                  bool *higher_priority_task_woken)
{
    if (higher_priority_task_woken) *higher_priority_task_woken = false;
    if (!queue_handle || !queue_handle->queueHandle || !item) return false;
    MacOsQueue *q = static_cast<MacOsQueue *>(queue_handle->queueHandle);

    std::unique_lock<std::mutex> lock(q->mtx, std::try_to_lock);
    if (!lock.owns_lock() || q->items.empty()) return false;

    std::memcpy(item, q->items.front().data(), q->item_size);
    q->items.pop();
    q->cv_not_full.notify_one();
    return true;
}
