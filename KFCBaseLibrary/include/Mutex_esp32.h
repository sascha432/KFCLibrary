/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <freertos/semphr.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>

#if DEBUG_MUTEX
    static constexpr TickType_t kPortDefaultDelay = std::min<TickType_t>(300 * 10000 / portTICK_PERIOD_MS, portMAX_DELAY);
#else
    static constexpr TickType_t kPortDefaultDelay = portMAX_DELAY;
#endif

// TODO some nasty workaround for issues with includes / #pragma
// but not sure how those files get mixed up. in some cases it includes
// these files (Mutex.h and the others) a second time, but cause of the
// pragma, it seems to skip other includes. if i just define them, it is
// duplicates. if i undefine them then other files say they are missing
// storing them in case they exist and popping them back works

#include <debug_helper.h>
#if DEBUG_MUTEX
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

#ifndef __LDBG_assert_printf
#define UNDEF_DEBUGS 1

#pragma push_macro("__LDBG_assert_printf")
#undef __LDBG_assert_printf
#pragma push_macro("__LDBG_assert")
#undef __LDBG_assert

#define __LDBG_assert_printf(...) ;
#define __LDBG_assert(...) ;

#endif

// ------------------------------------------------------------------------

class SemaphoreMutex
{
public:
    SemaphoreMutex(bool lock = false);
    SemaphoreMutex(xSemaphoreHandle handle, bool lock);
    ~SemaphoreMutex();

    void lock();
    void unlock();

    int _locked;
    xSemaphoreHandle _lock;
};

inline SemaphoreMutex::SemaphoreMutex(bool lock) :
    _locked(0),
    _lock(xSemaphoreCreateMutex())
{
    __LDBG_assert_printf(_lock == NULL, "xSemaphoreCreateMutex() res=%p", _lock);
    if (!lock) {
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
        if (_locked) {
            unlock();
        }
        vSemaphoreDelete(_lock);
    }
}

inline void SemaphoreMutex::lock()
{
    __LDBG_assert(_locked == 0);
    _locked++;
    BaseType_t err;
    while((err = xSemaphoreTake(_lock, kPortDefaultDelay)) != pdTRUE) {
        __LDBG_assert_printf(err != pdTRUE, "xSemaphoreTake err=%04x lock=%p", err, _lock);
    }
}

inline void SemaphoreMutex::unlock()
{
    __LDBG_assert(_locked != 0);
    BaseType_t err;
    if ((err = xSemaphoreGive(_lock)) == pdTRUE) {
        _locked--;
    }
    __LDBG_assert_printf(err != pdTRUE, "xSemaphoreGive err=%04x port=%p", err, _lock);
}

// ------------------------------------------------------------------------

class SemaphoreMutexStatic : public SemaphoreMutex
{
public:
    SemaphoreMutexStatic(bool lock = false) :
        SemaphoreMutex(NULL, false)
    {
        memset(&_buffer, 0, sizeof(_buffer));
        _lock = xSemaphoreCreateMutexStatic(&_buffer);
        __LDBG_assert_printf(_lock == NULL, "xSemaphoreCreateMutexStatic() res=%p", _lock);
        if (lock) {
            this->lock();
        }
    }
    SemaphoreMutexStatic(void *) :
        SemaphoreMutex(NULL, false)
    {
    }
    ~SemaphoreMutexStatic()
    {
        __LDBG_assert_printf(_lock == NULL, "_lock=%p", _lock);
        if (_lock) {
            if (_locked) {
                unlock();
            }
            _lock = NULL;
        }
    }

    void lock() {
        __LDBG_assert_printf(_lock == NULL, "_lock=%p", _lock);
        if (_lock == NULL) {
            return;
        }
        SemaphoreMutex::lock();
    }

    void unlock() {
        __LDBG_assert_printf(_lock == NULL, "_lock=%p", _lock);
        if (_lock == NULL) {
            return;
        }
        SemaphoreMutex::unlock();
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

    void lock();
    void unlock();

    void _create();

    int _locked;
    xSemaphoreHandle _lock;
};

inline SemaphoreMutexRecursive::SemaphoreMutexRecursive(bool lock) :
    _locked(0),
    _lock(xSemaphoreCreateRecursiveMutex())
{
    __LDBG_assert_printf(_lock == NULL, "xSemaphoreCreateRecursiveMutex() res=%p", _lock);
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
        if (_locked) {
            unlock();
        }
        vSemaphoreDelete(_lock);
    }
}

inline void SemaphoreMutexRecursive::lock()
{
    __LDBG_assert(_locked == 0);
    _locked++;
    BaseType_t err;
    while((err = xSemaphoreTakeRecursive(_lock, kPortDefaultDelay)) != pdTRUE) {
        __LDBG_assert_printf(err == pdTRUE, "xSemaphoreTakeRecursive err=%04x lock=%p", err, _lock);
    }

}

inline void SemaphoreMutexRecursive::unlock()
{
    __LDBG_assert(_locked != 0);
    BaseType_t err;
    if ((err = xSemaphoreGiveRecursive(_lock)) != pdTRUE) {
        __LDBG_assert_printf(err == pdTRUE, "xSemaphoreGiveRecursive err=%04x lock=%p", err, _lock);
        _locked--;
    }
}

// ------------------------------------------------------------------------


class SemaphoreMutexRecursiveStatic : public SemaphoreMutexRecursive
{
public:
    SemaphoreMutexRecursiveStatic(bool lock = false) :
        SemaphoreMutexRecursive(NULL, false)
    {
        memset(&_buffer, 0, sizeof(_buffer));
        _lock = xSemaphoreCreateMutexStatic(&_buffer);
        __LDBG_assert_printf(_lock == NULL, "xSemaphoreCreateMutexStatic() res=%p", _lock);
        if (lock) {
            this->lock();
        }
    }
    SemaphoreMutexRecursiveStatic(void *) :
        SemaphoreMutexRecursive(NULL, false)
    {
    }
    ~SemaphoreMutexRecursiveStatic()
    {
        if (_lock) {
            if (_locked) {
                SemaphoreMutexRecursive::unlock();
            }
            _lock = NULL;
        }
    }

    void lock() {
        if (_lock == NULL) {
            return;
        }
        SemaphoreMutexRecursive::lock();
    }

    void unlock() {
        if (_lock == NULL) {
            return;
        }
        SemaphoreMutexRecursive::unlock();
    }

    StaticSemaphore_t _buffer;
};

#ifdef UNDEF_DEBUGS
#pragma pop_macro("__LDBG_assert_printf")
#pragma pop_macro("__LDBG_assert")
#undef UNDEF_DEBUGS
#endif
