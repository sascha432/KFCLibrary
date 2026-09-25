/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <stdint.h>
#include <stdlib_noniso.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <pgmspace.h>
#include <memory>

class String;
class __FlashStringHelper;
#ifndef PGM_P
#define PGM_P const char *
#endif
#ifndef PGM_VOID_P
#define PGM_VOID_P const void *
#endif

#ifndef __CONSTEXPR17
#   if __GNUC__ >= 8
#       define __CONSTEXPR17 constexpr
#   else
#       define __CONSTEXPR17
#   endif
#endif

// ---------------------------------------------------------------------------
// PROGMEM/RAM pointer checks and __S()
//
// macros:
// __S(ptr)     -> const char *
// __SL(ptr)    -> strlen(ptr)
//
// compile time detection for printf "%s" to output. the return type is always const char * and
// might point to PROGMEM. requires to use PROGMEM safe functions and access memory with 32bit
// alignment. consider using PGM_read_* functions
//
// const char *
// PGM_P
// String
// IPAddress
//
// nullptr and invalid pointers are detected during runtime
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

extern const char SPGM_null[] PROGMEM;

#if ESP8266
    extern char _heap_start[];
#endif

#ifdef __cplusplus
}
#endif

#if defined(ESP8266)

#include "umm_malloc/umm_malloc.h"

#define PROGMEM_START_ADDRESS                               0x40200000U
#define PROGMEM_END_ADDRESS                                 0x402FEFF0U
#define HEAP_START_ADDRESS                                  0x3FFE8000U
#define HEAP_END_ADDRESS                                    0x3FFFFFFFU

inline bool is_HEAP_P(const void *ptr) {
    return (reinterpret_cast<const uintptr_t>(ptr) >= HEAP_START_ADDRESS) && (reinterpret_cast<const uintptr_t>(ptr) < HEAP_END_ADDRESS);
}

inline bool is_HEAP_P(const void *begin, const void *end) {
    return is_HEAP_P(begin) && is_HEAP_P(end);
}

inline bool is_PGM_P(const void *ptr) {
    return (reinterpret_cast<const uintptr_t>(ptr) >= PROGMEM_START_ADDRESS) && (reinterpret_cast<const uintptr_t>(ptr) < PROGMEM_END_ADDRESS);
}

inline bool is_PGM_P(const void *begin, const void *end) {
    return is_PGM_P(begin) && is_PGM_P(end);
}

inline bool is_aligned_PGM_P(const void * ptr)
{
    return ((reinterpret_cast<const uintptr_t>(ptr) & 0b11) == 0);
}

inline bool is_not_PGM_P_or_aligned(const void * ptr)
{
    return !is_PGM_P(ptr) || is_aligned_PGM_P(ptr);
}

#define __IS_SAFE_STR(str)              (is_HEAP_P(str) || is_PGM_P(str) ? str : __safeCString((const void *)str).c_str())

#elif ESP32

inline bool is_HEAP_P(const void *ptr) {
    return true;
}

inline bool is_PGM_P(const void *ptr) {
    return false;
}

inline bool is_aligned_PGM_P(const void * ptr)
{
    return (((const uintptr_t)ptr & 0b11) == 0);
}

inline bool is_not_PGM_P_or_aligned(const void * ptr)
{
    return is_aligned_PGM_P(ptr);
}

#define __IS_SAFE_STR(str)              str

#endif

#define pgm_read_byte_safe(ptr)         (is_HEAP_P(ptr) || is_PGM_P(ptr) ? pgm_read_byte(ptr) : -1)

// __S(str) for printf_P("%s") or any function requires const char * arguments
//
// const char *str                       __S(str) = str
// const __FlashStringHelper *str        __S(str) = (const char *)str
// String test;                         __S(test) = test.c_str()
// on the stack inside the a function call. the object gets destroyed when the function returns
//                                      __S(String()) = String().c_str()
// IPAddress addr;                       __S(addr) = addr.toString().c_str()
//                                      __S(IPAddress()) = IPAddress().toString().c_str()
// nullptr_t or any nullptr             __S((const char *)0) = "null"
// const void *                         __S(const void *)1767708) = String().printf("0x%08x", 0xff).c_str() = 0x001af91c

#define _S_STRLEN(str)                  (str ? strlen_P(__S(str)) : 0)
#define _S_STR(str)                     __S(str)
#define __S(str)                        __safeCString(str).c_str()

inline const String __safeCString(const void *ptr) {
    char buf[16];
    snprintf_P(buf, sizeof(buf), PSTR("0x%08x"), (uint32_t)ptr);
    return buf;
}

class SafeStringWrapper {
public:
    const char *_str;

    constexpr SafeStringWrapper() : _str(SPGM(null)) {}
    constexpr SafeStringWrapper(const char *str) : _str(str ? __IS_SAFE_STR(str) : SPGM(null)) {}
    constexpr SafeStringWrapper(const __FlashStringHelper *str) : _str(str ? __IS_SAFE_STR((PGM_P)str) : SPGM(null)) {}
    constexpr const char *c_str() const {
        return _str;
    }
};

inline const String __safeCString(const IPAddress &addr) {
    return addr.toString();
}

constexpr const String &__safeCString(const String &str) {
    return str;
}

constexpr const SafeStringWrapper __safeCString(const __FlashStringHelper *str) {
    return SafeStringWrapper(str);
}

constexpr const SafeStringWrapper __safeCString(const char *str) {
    return SafeStringWrapper(str);
}

constexpr const SafeStringWrapper __safeCString(nullptr_t ptr) {
    return SafeStringWrapper();
}

// ---------------------------------------------------------------------------
// additional low level PROGMEM string functions
// ---------------------------------------------------------------------------

char *strdup_P(PGM_P src);
PGM_P strchr_P(PGM_P str, int c);

// size == 0: returns ESZEROL
// str1 == nullptr: returns ESNULLP
// str2 == nullptr: returns -ESNULLP
// str1 == str2: returns 0 without comparing
int strncasecmp_P_P(PGM_P str1, PGM_P str2, size_t size);

#if ESP32

// comparing PROGMEM directly
#define strcasecmp_P_P(str1, str2) strcasecmp((str1), (str2))

#else

// SIZE_IRRELEVANT (= 0x7fffffff) comes from newlib's <sys/string.h>
// comparing PROGMEM directly
#define strcasecmp_P_P(str1, str2) strncasecmp_P_P((str1), (str2), SIZE_IRRELEVANT)

#endif

PGM_P strichr_P(PGM_P str1, int ch);

#define STRINGLIST_SEPARATOR                    ','

#define ESNULLP                                 ( 400 ) /* null ptr */
#define ESZEROL                                 ( 401 ) /* length is zero */
#define EOK                                     ( 0 )

// return index of the string in the list, 0 based
// all negative values are errors
// list == nullptr or find == nullptr: returns -1
// separator == 0 or nullptr: returns -1
int stringlist_find_P_P(PGM_P list, PGM_P find, PGM_P separator);
int stringlist_ifind_P_P(PGM_P list, PGM_P find, PGM_P separator);

inline int stringlist_find_P_P(PGM_P list, PGM_P find, char separator = STRINGLIST_SEPARATOR)
{
    if (!separator) {
        return -1;
    }
    if (!list || !find) {
        return -1;
    }
    const char separator_str[2] = { separator, 0 };
    return stringlist_find_P_P(list, find, separator_str);
}

inline __attribute__((__always_inline__))
int stringlist_find_P(const __FlashStringHelper *list, const char *find, char separator = STRINGLIST_SEPARATOR)
{
    return stringlist_find_P_P(reinterpret_cast<PGM_P>(list), find, separator);
}

inline int stringlist_ifind_P_P(PGM_P list, PGM_P find, char separator = STRINGLIST_SEPARATOR)
{
    if (!separator) {
        return -1;
    }
    if (!list || !find) {
        return -1;
    }
    const char separator_str[2] = { separator, 0 };
    return stringlist_ifind_P_P(list, find, separator_str);
}

inline __attribute__((__always_inline__))
int stringlist_ifind_P(const __FlashStringHelper *list, const char *find, char separator = STRINGLIST_SEPARATOR)
{
    return stringlist_ifind_P_P(reinterpret_cast<PGM_P>(list), find, separator);
}

// NOTE:
// string functions are nullptr safe
// only PGM_P is PROGMEM safe

// #define STRINGLIST_SEPARATOR                    ','
// #define STRLS                                   ","

// moved to misc_string.h

// find a string in a list of strings separated by a single characters
// -1 = not found, otherwise the number of the matching string
// int stringlist_find_P_P(PGM_P list, PGM_P find, char separator);

// multiple separators
// int stringlist_find_P_P(PGM_P list, PGM_P find, PGM_P separator);

// template<typename Tl, typename Tf, typename Tc>
// inline int stringlist_find_P_P(Tl list, Tf find, Tc separator) {
//     return stringlist_find_P_P(reinterpret_cast<PGM_P>(list), reinterpret_cast<PGM_P>(find), separator);
// }

// trim trailing zeros
// return length of the string
// output can be nullptr to get the length
size_t printTrimmedDouble(Print *output, double value, int digits = 6);
size_t printTrimmedFloat(Print *output, float value, int digits = 6);

// convert integer to binary string
// add a space every _Space bits
// use 0xff to disable extra spaces
// set _Reverse to true to display the lowest bit first
template<typename _Ta, uint8_t _Space = 8, bool _Reverse = false>
String decbin(_Ta value) {
    constexpr uint8_t addSpaces = (sizeof(_Ta) << 3) < _Space ? 0xff : _Space;
    constexpr uint8_t extraSpace = ((sizeof(_Ta) << 3) / addSpaces) + 1;
    constexpr _Ta mask = _Reverse ? 1 : 1 << ((sizeof(_Ta) << 3) - 1);
    char buf[(sizeof(_Ta) << 3) + extraSpace];
    auto endPtr = &buf[(sizeof(_Ta) << 3)];
    auto ptr = buf;
    uint8_t space;
    if __CONSTEXPR17 (addSpaces != 0xff) {
        space = addSpaces;
    }
    for(;;) {
        *ptr++ = (value & mask) ? '1' : '0';
        if (ptr >= endPtr) {
            break;
        }
        if __CONSTEXPR17 (_Reverse) {
            value >>= 1;
        }
        else {
            value <<= 1;
        }
        if __CONSTEXPR17 (addSpaces != 0xff) {
            if (--space == 0) {
                space = addSpaces;
                *ptr++ = ' ';
                endPtr++;
            }
        }
    }
    *ptr = 0;
    return buf;
}
