/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include "Arduino_compat.h"

// ------------------------------------------------------------------------

class SemaphoreMutex
{
public:
    SemaphoreMutex(bool doLock = false) :
        _locked(0)
    {
        if (doLock) {
            lock();
        }
    }
    ~SemaphoreMutex()
    {
        while(_locked > 0) {
            unlock();
        }
    }

    void lock() {
        _locked++;
        ets_intr_lock();
    }

    void unlock() {
        ets_intr_unlock();
        _locked--;
    }

    int _locked;
};

// ------------------------------------------------------------------------

class SemaphoreMutexStatic : public SemaphoreMutex
{
};

// ------------------------------------------------------------------------

class SemaphoreMutexRecursive : public SemaphoreMutex
{
};

// ------------------------------------------------------------------------

struct SemaphoreMutexRecursiveStatic : public SemaphoreMutexRecursive
{
};
