/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>
#include <misc.h>

#ifndef DEBUG_MUTEX
#    define DEBUG_MUTEX 1
#endif

#if ESP32
#    include "Mutex_esp32.h"
#elif ESP8266
#    include "Mutex_esp8266.h"
#elif _MSC_VER
#    include "Mutex_win32.h"
#endif

class MutexLock
{
public:
    MutexLock(SemaphoreMutex &pLock, bool performInitialLock = true) : _lock(pLock), _locked(0) {
        if (performInitialLock) {
            lock();
        }
    }
    ~MutexLock() {
        if (_locked) {
            _lock.unlock();
        }
    }
    bool lock() {
        if (!_locked) {
            _lock.lock();
            _locked++;
        }
        return _locked;
    }
    bool unlock() {
        if (_locked) {
            _lock.unlock();
            _locked--;
        }
        return _locked;
    }
    SemaphoreMutex &_lock;
    int _locked;
};

class MutexLockRecursive
{
public:
    MutexLockRecursive(SemaphoreMutexRecursive &pLock, bool performInitialLock = true) : _lock(pLock), _locked(0) {
        if (performInitialLock) {
            lock();
        }
    }
    ~MutexLockRecursive() {
        unlockAll();
    }
    bool lock() {
        _lock.lock();
        _locked++;
        return _locked;
    }
    bool unlock() {
        if (_locked) {
            _locked--;
            _lock.unlock();
        }
        return _locked;
    }
    void unlockAll() {
        while(_locked-- > 0) {
            _lock.unlock();
        }
    }
    SemaphoreMutexRecursive &_lock;
    int _locked;
};

#define MUTEX_LOCK_BLOCK(lock) \
    for(auto __lock = MutexLock(lock); __lock._locked; __lock.unlock())


#define MUTEX_LOCK_RECURSIVE_BLOCK(lock) \
    for(auto __lock = MutexLockRecursive(lock); __lock._locked; __lock.unlockAll())


#if DEBUG_MUTEX
#    include <debug_helper_disable.h>
#endif
