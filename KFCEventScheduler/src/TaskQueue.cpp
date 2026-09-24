/**
  Author: sascha_lammers@gmx.de
*/

#include "TaskQueue.h"

#include <new>

#if ESP32 && defined(CONFIG_HEAP_POISONING_COMPREHENSIVE)
    // heap_caps_check_integrity_all(), see process()
    #    include <esp_heap_caps.h>
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

// one queued unit of work, created with TaskQueue::allocItem()
// the queue takes ownership and deletes the item after the task has been popped
class TaskQueue::Item {
public:
    Item(const Item &item) = delete;
    Item &operator=(const Item &item) = delete;

    // true if a task has been assigned
    bool valid() const {
        return (bool)_task;
    }

private:
    friend class TaskQueue;

    explicit Item(Task task) : _next(nullptr), _task(std::move(task)) {
    }

    ItemPtr _next;
    Task _task;
};

TaskQueue::TaskQueue(size_t capacity) :
#if ESP32
    _queue(nullptr),
#endif
    _head(nullptr),
    _tail(nullptr),
    _capacity(capacity == kUnlimited ? kUnlimited : (capacity < 1 ? 1 : capacity)),
    _size(0)
    #if DEBUG_TASK_QUEUE
        , _dropped(0)
        , _processed(0)
        , _peak(0)
    #endif
{
    __LDBG_printf("this=%p capacity=%u", this, (unsigned)_capacity);
    TASK_QUEUE_ASSERT(_capacity == kUnlimited || _capacity >= 1);
#if ESP32
    if (_capacity != kUnlimited) {
        _queue = xQueueCreate(static_cast<UBaseType_t>(_capacity), sizeof(ItemPtr));
        TASK_QUEUE_ASSERT(_queue != nullptr);
    }
#endif
}

TaskQueue::~TaskQueue()
{
    __LDBG_printf("this=%p size=%u", this, (unsigned)size());
    clear();
#if ESP32
    if (_queue) {
        vQueueDelete(_queue);
        _queue = nullptr;
    }
#endif
}

TaskQueue::ItemPtr TaskQueue::allocItem(Task task)
{
    if (!task) {
        return nullptr;
    }
    auto item = new (std::nothrow) Item(std::move(task));
    TASK_QUEUE_ASSERT(item != nullptr);
    return item;
}

TaskQueue::ResultType TaskQueue::push(Task task)
{
    auto item = allocItem(std::move(task));
    if (!item) {
        __LDBG_printf("this=%p allocation failed", this);
        return ResultType::NO_MEMORY;
    }
    auto result = push(item);
    if (result != ResultType::SUCCESS) {
        // the item has not been queued
        delete item;
    }
    return result;
}

TaskQueue::ResultType TaskQueue::push(ItemPtr item)
{
    if (!item || !item->valid()) {
        __LDBG_printf("this=%p item=%p invalid", this, item);
        TASK_QUEUE_ASSERT(false);
        return ResultType::INVALID;
    }
#if ESP32
    if (_queue) {
        if (xQueueSend(_queue, &item, 0) != pdTRUE) {
            __LDBG_printf("this=%p capacity=%u full", this, (unsigned)_capacity);
            #if DEBUG_TASK_QUEUE
                _dropped++;
            #endif
            return ResultType::FULL;
        }
        #if DEBUG_TASK_QUEUE
            auto count = static_cast<size_t>(uxQueueMessagesWaiting(_queue));
            if (count > _peak) {
                _peak = count;
            }
        #endif
        return ResultType::SUCCESS;
    }
#endif
    auto result = _pushIntrusive(item);
    if (result != ResultType::SUCCESS) {
        __LDBG_printf("this=%p capacity=%u full", this, (unsigned)_capacity);
    }
    return result;
}

TaskQueue::ResultType TaskQueue::pushFromISR(ItemPtr item)
{
    // no logging and no assertions, this must be ISR safe
    if (!item || !item->valid()) {
        return ResultType::INVALID;
    }
#if ESP32
    if (_queue) {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(_queue, &item, &higherPriorityTaskWoken) != pdTRUE) {
            #if DEBUG_TASK_QUEUE
                _dropped++;
            #endif
            return ResultType::FULL;
        }
        // the item might have been queued while a task was waiting (not used by the loop consumer)
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
        #if DEBUG_TASK_QUEUE
            auto count = static_cast<size_t>(uxQueueMessagesWaiting(_queue));
            if (count > _peak) {
                _peak = count;
            }
        #endif
        return ResultType::SUCCESS;
    }
#endif
    return _pushIntrusive(item);
}

TaskQueue::ResultType TaskQueue::_pushIntrusive(ItemPtr item)
{
    ResultType result = ResultType::SUCCESS;
    _lock();
    if (_capacity != kUnlimited && _size >= _capacity) {
        #if DEBUG_TASK_QUEUE
            _dropped++;
        #endif
        result = ResultType::FULL;
    }
    else {
        item->_next = nullptr;
        if (_tail) {
            _tail->_next = item;
        }
        else {
            _head = item;
        }
        _tail = item;
        _size++;
        #if DEBUG_TASK_QUEUE
            if (_size > _peak) {
                _peak = _size;
            }
        #endif
    }
    _unlock();
    return result;
}

TaskQueue::ItemPtr TaskQueue::_popIntrusive()
{
    _lock();
    auto item = _head;
    if (item) {
        _head = item->_next;
        if (!_head) {
            _tail = nullptr;
        }
        item->_next = nullptr;
        _size--;
    }
    _unlock();
    return item;
}

bool TaskQueue::pop(Task &task)
{
    ItemPtr item = nullptr;
#if ESP32
    if (_queue) {
        if (xQueueReceive(_queue, &item, 0) != pdTRUE) {
            item = nullptr;
        }
    }
    else
#endif
    {
        item = _popIntrusive();
    }
    if (!item) {
        return false;
    }
    task = std::move(item->_task);
    delete item;
    return true;
}

size_t TaskQueue::process(size_t maxItems, uint32_t maxRuntimeMillis)
{
    size_t count = 0;
    const uint32_t startTime = maxRuntimeMillis ? millis() : 0;
    while (count < maxItems) {
        Task task;
        if (!pop(task)) {
            break;
        }
        task();
        #if ESP32 && defined(CONFIG_HEAP_POISONING_COMPREHENSIVE)
            heap_caps_check_integrity_all(true);
        #endif
        count++;
        if (maxRuntimeMillis && (millis() - startTime) >= maxRuntimeMillis) {
            __LDBG_printf("this=%p runtime limit reached, executed=%u", this, (unsigned)count);
            break;
        }
    }
    #if DEBUG_TASK_QUEUE
        _processed += count;
    #endif
    return count;
}

void TaskQueue::clear()
{
    size_t count = 0;
    while (true) {
        Task task;
        if (!pop(task)) {
            break;
        }
        count++;
    }
    if (count) {
        __LDBG_printf("this=%p removed=%u", this, (unsigned)count);
    }
}

size_t TaskQueue::size() const
{
#if ESP32
    if (_queue) {
        return static_cast<size_t>(uxQueueMessagesWaiting(_queue));
    }
#endif
    return _size;
}

size_t TaskQueue::capacity() const
{
    return _capacity;
}

#if DEBUG_TASK_QUEUE

size_t TaskQueue::dropped() const
{
    return _dropped;
}

size_t TaskQueue::processed() const
{
    return _processed;
}

size_t TaskQueue::peakSize() const
{
    return _peak;
}

void TaskQueue::resetStatistics()
{
    _dropped = 0;
    _processed = 0;
    _peak = size();
}

#endif

void TaskQueue::_lock()
{
#if ESP8266
    ets_intr_lock();
#elif ESP32
    portENTER_CRITICAL(&_mux);
#else
    _mutex.lock();
#endif
}

void TaskQueue::_unlock()
{
#if ESP8266
    ets_intr_unlock();
#elif ESP32
    portEXIT_CRITICAL(&_mux);
#else
    _mutex.unlock();
#endif
}

#ifndef _MSC_VER
#    pragma GCC pop_options
#endif
