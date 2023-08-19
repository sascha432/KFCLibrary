/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <freertos/semphr.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>

#if DEBUG_MUTEX
    static constexpr TickType_t kPortDefaultDelay = std::min<TickType_t>(pdMS_TO_TICKS(1000), portMAX_DELAY);
#else
    static constexpr TickType_t kPortDefaultDelay = portMAX_DELAY;
#endif

#include <debug_helper.h>
#if DEBUG_MUTEX
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

// ------------------------------------------------------------------------

class SemaphoreMutex
{
public:
    SemaphoreMutex(bool lock = false);
    SemaphoreMutex(xSemaphoreHandle handle, bool lock);
    ~SemaphoreMutex();

    bool lock();
    bool unlock();

    int _locked;
    xSemaphoreHandle _lock;
};

inline SemaphoreMutex::SemaphoreMutex(bool lock) :
    _locked(0),
    _lock(xSemaphoreCreateMutex())
{
    __LDBG_assert_printf(_lock != NULL, "xSemaphoreCreateMutex() res=%p", _lock);
    if (lock) {
        this->lock();
    }
}

inline SemaphoreMutex::SemaphoreMutex(xSemaphoreHandle handle, bool lock) :
    _locked(0),
    _lock(handle)
{
    if (lock) {
        this->lock();
    }
}

inline SemaphoreMutex::~SemaphoreMutex()
{
    if (_lock != NULL) {
        unlock();
        vSemaphoreDelete(_lock);
        #if DEBUG_MUTEX
            _lock = NULL;
        #endif
    }
}

inline bool SemaphoreMutex::lock()
{
    _locked++;
    while(xSemaphoreTake(_lock, kPortDefaultDelay) != pdTRUE) {
        __LDBG_assert_printf(false, "xSemaphoreTake locked=%", _locked);
    }
    return true;
}

inline bool SemaphoreMutex::unlock()
{
    BaseType_t err;
    if ((err = xSemaphoreGive(_lock)) == pdTRUE) {
        _locked--;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------

class SemaphoreMutexStatic : public SemaphoreMutex
{
public:
    SemaphoreMutexStatic(bool lock = false) :
        SemaphoreMutex(xSemaphoreCreateMutexStatic(&_buffer), lock)
    {
    }
    ~SemaphoreMutexStatic()
    {
        __LDBG_assert_printf(_lock != NULL, "_lock=%p", _lock);
        if (_lock) {
            unlock();
            _lock = NULL;
        }
    }

    bool lock() {
        __LDBG_assert_printf(_lock != NULL, "_lock=%p", _lock);
        return SemaphoreMutex::lock();
    }

    bool unlock() {
        __LDBG_assert_printf(_lock != NULL, "_lock=%p", _lock);
        return SemaphoreMutex::unlock();
    }

    StaticSemaphore_t _buffer;
};

// ------------------------------------------------------------------------

class SemaphoreMutexRecursive
{
public:
    SemaphoreMutexRecursive(bool lock = false);
    SemaphoreMutexRecursive(xSemaphoreHandle handle, bool lock);
    ~SemaphoreMutexRecursive();

    bool lock();
    bool unlock();

    int _locked;
    xSemaphoreHandle _lock;
};

inline SemaphoreMutexRecursive::SemaphoreMutexRecursive(bool lock) :
    _locked(0),
    _lock(xSemaphoreCreateRecursiveMutex())
{
    __LDBG_assert_printf(_lock != NULL, "xSemaphoreCreateRecursiveMutex() res=%p", _lock);
    if (lock) {
        this->lock();
    }
}

inline SemaphoreMutexRecursive::SemaphoreMutexRecursive(xSemaphoreHandle handle, bool lock) :
    _locked(0),
    _lock(handle)
{
    if (lock) {
        this->lock();
    }
}

inline SemaphoreMutexRecursive::~SemaphoreMutexRecursive()
{
    if (_lock != NULL) {
        while(unlock()) { // unlock all
        }
        vSemaphoreDelete(_lock);
    }
}

inline bool SemaphoreMutexRecursive::lock()
{
    _locked++;
    while(xSemaphoreTakeRecursive(_lock, kPortDefaultDelay) != pdTRUE) {
        __LDBG_assert_printf(false, "xSemaphoreTakeRecursive locked=%u", _locked);
    }
    return true;
}

inline bool SemaphoreMutexRecursive::unlock()
{
    BaseType_t err;
    if ((err = xSemaphoreGiveRecursive(_lock)) == pdTRUE) {
        _locked--;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------


class SemaphoreMutexRecursiveStatic : public SemaphoreMutexRecursive
{
public:
    SemaphoreMutexRecursiveStatic(bool lock = false) :
        SemaphoreMutexRecursive(xSemaphoreCreateMutexStatic(&_buffer), lock)
    {
    }
    ~SemaphoreMutexRecursiveStatic()
    {
        if (_lock) {
            while(unlock()) { // unlock all
            }
            _lock = NULL;
        }
    }

    bool lock() {
        return SemaphoreMutexRecursive::lock();
    }

    bool unlock() {
        return SemaphoreMutexRecursive::unlock();
    }

    StaticSemaphore_t _buffer;
};
