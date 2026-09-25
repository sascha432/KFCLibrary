/**
 * Author: sascha_lammers@gmx.de
 */

/**
 * Quick and dirty library to compile ESP8266 and ESP32 code with MSVC++ as native Win32 Console Application
 */

#pragma once

// The Arduino cores of this project are patched (github.com/sascha432) and add methods to String
// that do not exist in the stock Arduino cores: __release(), __getAllocSize(), __getMemorySize(),
// rtrim/ltrim(...), startsWith/endsWith(char), startsWithIgnoreCase, replace() -> bool,
// trim() -> String&, ...
//
// the code for both is kept, the build environment selects the core:
//      conf/common_esp8266.ini     0   stock platformio/framework-arduinoespressif8266
//      conf/common_esp32.ini       1   sascha432/arduino-esp32.git
// the default is 0, so a stock core always compiles
#ifndef WSTRING_HAVE_EXTENDED_API
#    define WSTRING_HAVE_EXTENDED_API 0
#endif

#if DEBUG && _MSC_VER
#ifndef _DEBUG
#error _DEBUG required
#endif

#if ARDUINO <= 100
#error ARDUINO>100 required
#endif

#if !UNICODE || !_UNICODE
#error UNICODE and _UNICODE required
#endif

#if !_CRT_SECURE_NO_WARNINGS
#error _CRT_SECURE_NO_WARNINGS required
#endif

#if _CRTDBG_MAP_ALLOC
#define new                                             new( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define CHECK_MEMORY(...)                               if (_CrtCheckMemory() == false) { __debugbreak(); }
#else
#define CHECK_MEMORY(...)                               ;
//#warning _CRTDBG_MAP_ALLOC=0
#endif

#else
#define CHECK_MEMORY(...)                               ;
#endif

#if _MSC_VER

#ifndef __attribute__
#define __attribute__(...)
#endif

// place on heap for memory check
#define stack_array(name, type, size)                   size_t name##_unique_ptr_size = sizeof(type[size]); auto name##_unique_ptr = std::unique_ptr<type[]>(new type[size]); auto *name = (name##_unique_ptr).get()
#define sizeof_stack_array(name)                        name##_unique_ptr_size
#else
#define stack_array(name, type, size)                   auto name[size]
#define sizeof_stack_array(name)                        sizeof(name)
#endif

//
// NOTE:
// if a flash string is not defined, run
// pio run -t buildspgm
// to rebuild the string database
//
#define PROGMEM_STRING_ID(name)                         SPGM_##name

#if _MSC_VER

#    define PROGMEM_STRING_DECL(name)       extern const char *PROGMEM_STRING_ID(name) PROGMEM;
#    define PROGMEM_STRING_DEF(name, value) const char *PROGMEM_STRING_ID(name) PROGMEM = (const char *)__register_flash_memory(value, constexpr_strlen(value) + 1, PSTR_ALIGN);

#else

#    if ESP32

#        define PROGMEM_STRING_DECL(name)       extern const char PROGMEM_STRING_ID(name)[] PROGMEM;
#        define PROGMEM_STRING_DEF(name, value) const char PROGMEM_STRING_ID(name)[] PROGMEM = { value };

#    else

#        define PROGMEM_STRING_DECL(name)       extern const char PROGMEM_STRING_ID(name)[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM;
#        define PROGMEM_STRING_DEF(name, value) const char PROGMEM_STRING_ID(name)[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM = { value };

#    endif

#endif

class String;
class __FlashStringHelper;
#ifndef PGM_P
#define PGM_P const char *
#endif
#ifndef PGM_VOID_P
#define PGM_VOID_P const void *
#endif

#define ESNULLP                                 ( 400 ) /* null ptr */
#define ESZEROL                                 ( 401 ) /* length is zero */
#define EOK                                     ( 0 )

#include <tuple>

#if defined(ESP32)

#include <Arduino.h>
#include <global.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>

#define HAVE_NVS_FLASH 1

class __FlashStringHelper;

#    if USE_LITTLEFS
#        include <LittleFS.h>
#        define KFCFS LittleFS
#        define KFCFS_begin()      KFCFS.begin()
#        define KFCFS_MAX_FILE_LEN 31
#        define KFCFS_MAX_PATH_LEN 127
#    else
#        include <FS.h>
#        define KFCFS              SPIFFS
#        define KFCFS_begin()      KFCFS.begin()
#        define KFCFS_MAX_FILE_LEN 31
// includes directory slashes and filename
#        define KFCFS_MAX_PATH_LEN KFCFS_MAX_FILE_LEN
#    endif

#   if ESP32
#        define KFCFS_openDir(dir) Dir(dir)
#    else
#        define KFCFS_openDir(dir) Dir(KFCFS.open(dir, fs::FileOpenMode::read))
#    endif

#include "esp32_compat.h"

// the project's own PROGMEM string/pointer helpers (__S(), is_HEAP_P/is_PGM_P, strcasecmp_P_P, strchr_P, ...)
// the header has an #if ESP32 branch, on ESP32 they are plain libc calls
#include "misc_string.h"

#    define SPGM(name, ...)  PROGMEM_STRING_ID(name)
#    define FSPGM(name, ...) reinterpret_cast<const __FlashStringHelper *>(SPGM(name))
#    define PSPGM(name, ...) (PGM_P)(SPGM(name))

#    ifndef __attribute__packed__
#        define __attribute__packed__    __attribute__((packed))
#        define __attribute__unaligned__ __attribute__((__aligned__(1)))
#        define PSTR1(str)               PSTRN(str, 1)
#    endif

#    include "debug_helper.h"
#    include "misc.h"

#elif defined(ESP8266)

#include <Arduino.h>
#include <global.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiType.h>
#include <WiFiUdp.h>

inline __attribute__((__always_inline__)) void KFCFS_begin_func();

#if USE_LITTLEFS
#        include <LittleFS.h>
#        define KFCFS              LittleFS
#        define KFCFS_begin()      KFCFS_begin_func()
#        define KFCFS_openDir(dir) KFCFS.openDir(dir)
#        define KFCFS_MAX_FILE_LEN 31
#        define KFCFS_MAX_PATH_LEN 127
#    else
#        include <FS.h>
#        define KFCFS              SPIFFS
#        define KFCFS_begin()      KFCFS_begin_func()
#        define KFCFS_openDir(dir) KFCFS.openDir(dir)
#        define KFCFS_MAX_FILE_LEN 31
// includes directory slashes and filename
#        define KFCFS_MAX_PATH_LEN KFCFS_MAX_FILE_LEN
#endif

#if defined(HAVE_GDBSTUB) && HAVE_GDBSTUB

#include <GDBStub.h>

extern "C" void gdbstub_do_break(void);
extern "C" bool gdb_present(void);

#define gdb_do_break gdbstub_do_break

#endif

#include "esp8266_compat.h"

#    define SPGM(name, ...)  PROGMEM_STRING_ID(name)
#    define FSPGM(name, ...) FPSTR(SPGM(name))
#    define PSPGM(name, ...) (PGM_P)(SPGM(name))

#    ifndef __attribute__packed__
#        define __attribute_packed__     __attribute__((packed))
#        define __attribute__packed__    __attribute__((packed))
#        define __attribute__unaligned__ __attribute__((__aligned__(1)))
#        define PSTR1(str)               PSTRN(str, 1)
#    endif

#    ifndef PWMRANGE
#        define PWMRANGE 1023
#    endif

#    include "misc_string.h"
#    include "debug_helper.h"
#    include "misc.h"

#elif _MSC_VER

#define NOMINMAX
#if !defined(_CRTDBG_MAP_ALLOC) && DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#define DEBUGV(...) ;

#include <stdint.h>
#include <crtdbg.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <strsafe.h>
#include <vector>
#include <iostream>
#include <Psapi.h>
#include <assert.h>
#include <CRTDBG.h>
#include <pgmspace.h>

#include "win32_compat.h"
#include "WString.h"

#define KFCFS                                           SPIFFS
#define KFCFS_MAX_FILE_LEN                              31
// includes directory slashes and filename
#define KFCFS_MAX_PATH_LEN                              KFCFS_MAX_FILE_LEN

#include <ets_sys_win32.h>
#include <ets_timer_win32.h>

#define __attribute__(a)

#ifndef DEBUG_OUTPUT
#define DEBUG_OUTPUT Serial
#endif

#include <global.h>

void init_winsock();

extern "C" uint32_t crc32(const void *data, size_t length, uint32_t crc = ~0U);

#ifndef strdup
#define strdup _strdup
#endif

uint16_t __builtin_bswap16(uint16_t);

#include "Arduino.h"

#define PROGMEM

#define SPGM(name, ...)                                 PROGMEM_STRING_ID(name)
#define FSPGM(name, ...)                                FPSTR(SPGM(name))
#define PSPGM(name, ...)                                (PGM_P)(SPGM(name))

#include <pgmspace.h>

void throwException(PGM_P message);

#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "FS.h"
#include "Serial.h"
#include "WiFi.h"
#include "WiFiUDP.h"
#include "ESP.h"

#include "debug_helper.h"
#include "misc.h"

extern const String emptyString;

#else

#error Platform not supported

#endif

#include "FileOpenMode.h"
#include "constexpr_tools.h"
#include <stl_ext/utility.h>

#if __GNUC__

static size_t constexpr constexpr_strlen(const char *str) noexcept
{
    return __builtin_strlen(str);
}

#else

constexpr size_t constexpr_strlen(const char *s) noexcept
{
    return s ? (*s ? 1 + constexpr_strlen(s + 1) : 0) : 0;
}

#endif

#ifndef _STRINGIFY
#define _STRINGIFY(...)                     ___STRINGIFY(__VA_ARGS__)
#endif
#define ___STRINGIFY(...)                   #__VA_ARGS__

// reinterpret_cast FPSTR
#ifndef RFPSTR
#define RFPSTR(str)                         reinterpret_cast<PGM_P>(str)
#endif

// equivalent to __FlashStringHelper/const char *
// __FlashBufferHelper/const uint8_t *
class __FlashBufferHelper;

namespace __va_args__
{
    template<typename ...Args>
    constexpr std::size_t va_count(Args &&...) { return sizeof...(Args); }
}

#define __VA_ARGS_COUNT__(...)                          __va_args__::va_count(__VA_ARGS__)


#define DUMP_BINARY_DEFAULTS ~0U
#define DUMP_BINARY_NO_TITLE nullptr

extern "C" {
    // negative len won't output anything
    void __dump_binary(const void *ptr, int len, size_t perLine = DUMP_BINARY_DEFAULTS, PGM_P title = DUMP_BINARY_NO_TITLE, uint8_t groupBytes = static_cast<uint8_t>(DUMP_BINARY_DEFAULTS));
    void __dump_binary_to(Print &output, const void *ptr, int len, size_t perLine = DUMP_BINARY_DEFAULTS, PGM_P title = DUMP_BINARY_NO_TITLE, uint8_t groupBytes = static_cast<uint8_t>(DUMP_BINARY_DEFAULTS));
}

// the ESP8266 core has File::fullName(), the stock ESP32 core names it path()
inline
__attribute__((__always_inline__))
const char *fullName(const fs::File &file) {
#if ESP32
    return file.path();
#else
    return file.fullName();
#endif
}

#if ESP32
// the ESP8266 core provides FSInfo and FS::info(), the stock ESP32 core has neither
// (LittleFS only exports totalBytes()/usedBytes())
struct FSInfo {
    size_t totalBytes;
    size_t usedBytes;
    size_t blockSize;
    size_t pageSize;
    size_t maxOpenFiles;
    size_t maxPathLength;
};
#endif

inline
__attribute__((__always_inline__))
void getFSInfo(FSInfo &info) {
#if ESP32
    memset(&info, 0, sizeof(info));
    info.totalBytes = KFCFS.totalBytes();
    info.usedBytes = KFCFS.usedBytes();
    // not exported by the stock core
    info.blockSize = 4096;
    info.maxOpenFiles = 5;
    info.maxPathLength = KFCFS_MAX_PATH_LEN;
#else
    KFCFS.info(info);
#endif
}

// the ESP8266 core has ESP.random(), the ESP32 core esp_random()/esp_fill_random()
inline
__attribute__((__always_inline__))
uint32_t getRandom() {
#if ESP32
    return esp_random();
#else
    return ESP.random();
#endif
}

inline
__attribute__((__always_inline__))
void getRandom(uint8_t *buffer, size_t size) {
#if ESP32
    esp_fill_random(buffer, size);
#else
    ESP.random(buffer, size);
#endif
}

// the ESP8266 core has ESP.getHeapFragmentation(), ESP32 only reports the largest free block
inline
__attribute__((__always_inline__))
uint8_t getHeapFragmentation() {
#if ESP32
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (!freeHeap) {
        return 0;
    }
    return static_cast<uint8_t>(100 - (heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) * 100 / freeHeap));
#else
    return static_cast<uint8_t>(ESP.getHeapFragmentation());
#endif
}

inline
__attribute__((__always_inline__))
void KFCFS_begin_func() {
    // DebugMeasureTimer _mt(PSTR("KFCFS_begin"));
    if (!KFCFS.begin()) {
        __DBG_printf_E("failed to start FS");
    }
}

#if _MSC_VER

#include "../../../include/spgm_auto_strings.h"

#else

#include <spgm_auto_strings.h>
#include <spgm_auto_def.h>

#if ESP8266
#include <coredecls.h>
#endif

#endif

// ----------------------------------------------------------------------------
// comparing a String with a flash string in either order
// ----------------------------------------------------------------------------
// the String class of the patched cores has the member operators for `str == F("...")` only,
// `F("...") == str` does not compile at all: a member operator can never have the class on the
// right and C++17 does not consider member candidates when the left operand is not a class type
//
// the non-member operators below handle the reversed order without a temporary String or an
// allocation - the stock Arduino cores have to convert the flash string into a String object
// (short literals end up in the SSO buffer, everything else allocates)

inline bool operator ==(const __FlashStringHelper *lhs, const String &rhs) {
    return rhs.equals(lhs);
}

inline bool operator !=(const __FlashStringHelper *lhs, const String &rhs) {
    return !rhs.equals(lhs);
}
