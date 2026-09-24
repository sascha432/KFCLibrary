/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>
#include <functional>
#include <Mutex.h>

#if ESP32
#    include <freertos/FreeRTOS.h>
#    include <freertos/queue.h>
#    include <freertos/portmacro.h>
#endif

#ifndef DEBUG_TASK_QUEUE
#    define DEBUG_TASK_QUEUE (1 || defined(DEBUG_ALL))
#endif

#ifndef TASK_QUEUE_ASSERT
// #define TASK_QUEUE_ASSERT(cond)                     assert(cond)
#    define TASK_QUEUE_ASSERT(cond) __LDBG_assert(cond)
#endif

#if DEBUG_TASK_QUEUE
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

#ifndef _MSC_VER
#    pragma GCC push_options
#    pragma GCC optimize("O3")
#endif

//
// TaskQueue
//
// FIFO queue for tasks (std::function<void(void)>) with the same basic semantics as a FreeRTOS
// queue, available on ESP32 and ESP8266.
//
// - producers: any context (main loop, other tasks, WiFi/sys callbacks) and, using a
//   pre-allocated item, interrupt handlers. A std::function can not be created inside an ISR,
//   therefore allocItem() has to be called outside and pushFromISR() inside the ISR.
// - consumer: a single task, normally the main loop, calling pop()/process()/clear()
//
//   ESP32    xQueueCreate()/xQueueSend()/xQueueReceive() (capacity != kUnlimited)
//            intrusive list + portENTER_CRITICAL() (capacity == kUnlimited)
//   ESP8266  intrusive list + ets_intr_lock() (ISR safe)
//   _MSC_VER intrusive list + std::mutex
//
// The queue is bounded (kDefaultCapacity entries) and push() fails with ResultType::FULL instead
// of silently dropping entries. Use kUnlimited for a queue that grows as needed.
//
// size() is exact. The counters returned by dropped()/processed()/peakSize() and the members behind
// them are only compiled in with DEBUG_TASK_QUEUE (1 by default with DEBUG_ALL), they are advisory
// and might be off by a few counts while multiple producers are pushing concurrently.
//
class TaskQueue {
public:
    class Item;

    using ItemPtr = Item *;
    using Task = std::function<void(void)>;

    enum class ResultType : int8_t {
        SUCCESS = 0,                    // item has been queued
        FULL = -1,                      // capacity reached, item has not been queued
        EMPTY = -2,                     // queue is empty (pop() returns false)
        INVALID = -3,                   // no item or no task assigned
        NO_MEMORY = -4,                 // the item could not be allocated
    };

    static constexpr size_t kDefaultCapacity = 8;
    // no limit, the queue grows as needed (no FreeRTOS queue is used on ESP32)
    static constexpr size_t kUnlimited = 0;
    // process() all queued items
    static constexpr size_t kAllItems = ~0U;

public:
    explicit TaskQueue(size_t capacity = kDefaultCapacity);
    ~TaskQueue();

    TaskQueue(const TaskQueue &) = delete;
    TaskQueue &operator=(const TaskQueue &) = delete;

    // ---- producer ----

    // thread safe, must not be called from an ISR
    ResultType push(Task task);
    // thread safe, no allocation, takes ownership of the item
    ResultType push(ItemPtr item);
    // ISR safe, no allocation, takes ownership of the item
    ResultType pushFromISR(ItemPtr item);
    // create an item for push()/pushFromISR(), must not be called from an ISR
    static ItemPtr allocItem(Task task);

    // ---- consumer (single task) ----

    // returns false if the queue is empty
    bool pop(Task &task);
    // execute up to maxItems tasks or until maxRuntimeMillis elapsed (0 = no limit)
    size_t process(size_t maxItems = kAllItems, uint32_t maxRuntimeMillis = 0);
    // remove all queued items without executing them
    void clear();

    // ---- state ----

    // number of queued items
    size_t size() const;
    // configured capacity, kUnlimited if there is no limit
    size_t capacity() const;

    #if DEBUG_TASK_QUEUE

    // ---- statistics (DEBUG_TASK_QUEUE only) ----

    // number of items rejected because the queue was full
    size_t dropped() const;
    // number of tasks executed by process()
    size_t processed() const;
    // highest number of queued items so far
    size_t peakSize() const;
    // clears dropped()/processed() and sets peakSize() to the current size()
    void resetStatistics();

    #endif

    inline bool empty() const {
        return size() == 0;
    }

private:
    ResultType _pushIntrusive(ItemPtr item);
    ItemPtr _popIntrusive();
    void _lock();
    void _unlock();

private:
#if ESP32
    QueueHandle_t _queue;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#endif
#if _MSC_VER
    SemaphoreMutex _mutex;
#endif
    // used by ESP8266/_MSC_VER and by ESP32 if capacity is kUnlimited
    ItemPtr _head;
    ItemPtr _tail;
    size_t _capacity;
    size_t _size;
    #if DEBUG_TASK_QUEUE
        size_t _dropped;
        size_t _processed;
        size_t _peak;
    #endif
};

#ifndef _MSC_VER
#    pragma GCC pop_options
#endif
