/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>
#include <misc.h>

#ifndef DEBUG_MUTEX
#    define DEBUG_MUTEX (0 || defined(DEBUG_ALL))
#endif

#if ESP32
#    include "Mutex_esp32.h"
#elif ESP8266
#    include "Mutex_esp8266.h"
#elif _MSC_VER
#    include "Mutex_win32.h"
#endif

template<typename _SemaphoreType>
class MutexLockTempl
{
public:
    MutexLockTempl(_SemaphoreType &pLock, bool performInitialLock = true) : _lock(pLock), _locked(0) {
        if (performInitialLock) {
            lock();
        }
    }
    ~MutexLockTempl() {
        if (_locked) {
            _lock.unlock();
        }
    }
    bool lock() {
        if (_lock.lock()) {
            _locked++;
        }
        return _locked;
    }
    bool unlock() {
        if (_lock.unlock()) {
            _locked--;
        }
        return _locked;
    }
    _SemaphoreType &_lock;
    int _locked;
};

using MutexLock = MutexLockTempl<SemaphoreMutex>;
using MutexLockRecursive = MutexLockTempl<SemaphoreMutexRecursive>;

#define MUTEX_LOCK_BLOCK(lock) \
    for(auto __lock = MutexLockTempl(lock); __lock._locked; __lock.unlock())


#define MUTEX_LOCK_RECURSIVE_BLOCK(lock) \
    for(auto __lock = MutexLockTempl(lock); __lock._locked; __lock.unlock())


#if DEBUG_MUTEX
#    include <debug_helper_disable.h>
#endif
