
/**
 * Author: sascha_lammers@gmx.de
 */

// replaced 2 print functions that write directly to the console without the Serial object, which is not initialized at that point

#pragma once

#undef __LDBG_printf
#if DEBUG_RESET_DETECTOR
#    if ESP32
#        define __LDBG_printf(fmt, ...) ::printf_P(PSTR("DBG%04u %s:%u: " fmt "\n"), micros() / 1000, __BASENAME_FILE__, __LINE__, ##__VA_ARGS__)
#    else
#        define __LDBG_printf(fmt, ...) ::printf_P(PSTR("DBG%04u %s:%u: " fmt "\n"), micros() / 1000, __BASENAME_FILE__, __LINE__, ##__VA_ARGS__)
#    endif
#    undef __DBG_printf
#    define __DBG_printf __LDBG_printf
#else
#    define __LDBG_printf(...)
#endif
