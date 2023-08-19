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
        while(unlock()) {
        }
    }

    bool lock() {
        ets_intr_lock();
        _locked++;
        return true;
    }

    bool unlock() {
        bool result = _locked > 0;
        if (result) {
            _locked--;
            ets_intr_unlock();
        }
        return result;
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
