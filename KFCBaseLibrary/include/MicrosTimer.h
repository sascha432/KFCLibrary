/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <Arduino_compat.h>

class MicrosTimer {
public:
    typedef uint32_t timer_t;

public:
    MicrosTimer() : _valid(false) {

    }
    MicrosTimer(timer_t startTime) : _start(startTime), _valid(true) {
    }

    // returns the time passed since start was called
    // 0 means either that start has not been called yet or the maximum time limit has been expired
    inline __attribute__((always_inline)) timer_t getTime() {
        return getTime(micros());
    }

    inline __attribute__((always_inline)) timer_t getTimeConst() const {
        return getTimeConst(micros());
    }

    // (re)start timer
    inline __attribute__((always_inline)) void start() {
        return start(micros());
    }

    timer_t getTime(timer_t time);
    timer_t getTimeConst(timer_t time) const;
    void start(timer_t time);

    inline bool isValid() {
        return getTime() != 0;
    }

private:
    timer_t _start;
    bool _valid;
};
