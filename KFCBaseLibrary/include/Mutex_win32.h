/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <assert.h>
#include <mutex>

class SemaphoreMutex {
public:
    SemaphoreMutex() : _locked(false)
    {
    }

    ~SemaphoreMutex() {
        _ASSERT(_locked == false); // "destroyed locked SemaphoreMutex"
    }

    void lock() {
        _locked = true;
        _mutex.lock();
    }

    void unlock() {
        _mutex.unlock();
        _locked = false;
    }

    std::mutex _mutex;
    std::atomic_bool _locked;
};

class SemaphoreMutexRecursive : public SemaphoreMutex {
};
