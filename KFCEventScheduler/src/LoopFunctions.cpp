/**
  Author: sascha_lammers@gmx.de
*/

#include "LoopFunctions.h"

#if DEBUG_LOOP_FUNCTIONS
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

#pragma GCC push_options
#pragma GCC optimize("O3")

LoopFunctions::FunctionsVector LoopFunctions::_functions;

#if ESP32

#include <vector>

#define SCHEDULED_FN_MAX_COUNT 32

std::vector<std::function<void(void)>> scheduled_functions;

bool IRAM_ATTR schedule_function (const std::function<void(void)> &fn)
{
    if (fn) {
        if (scheduled_functions.size() < SCHEDULED_FN_MAX_COUNT) {
            scheduled_functions.emplace_back(fn);
            return true;
        }
    }
    return false;
}

#endif

#pragma GCC pop_options
