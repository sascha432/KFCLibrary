/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <stdint.h>

#if ESP8266

#    include <esp_partition.h>

#else

#    include <Esp.h>

#    define FLASH_SECTOR_SIZE              0x1000
#    define SECTION_FLASH_START_ADDRESS    0x40200000U
#    define SECTION_FLASH_END_ADDRESS      0x40300000U // 1MB, 256 sectors or 1048576 byte are addressable
#    define SECTION_FLASH_START_ADDR(name) ((uint32_t)&_##name##_start)
#    define SECTION_FLASH_END_ADDR(name)   ((uint32_t)&_##name##_end)
#    define SECTION_CALC_SIZE(name)        ((((uint32_t)&_##name##_end) - ((uint32_t)&_##name##_start)) + FLASH_SECTOR_SIZE)
#    define SECTION_START_ADDR(name)       (((uint32_t)&_##name##_start) - SECTION_FLASH_START_ADDRESS)
#    define SECTION_EXTERN_UINT32(name)    extern "C" uint32_t _##name##_start; extern "C" uint32_t _##name##_end;

#endif

#if _MSC_VER

#    ifndef FLASH_MEMORY_STORAGE_FILE
#        define FLASH_MEMORY_STORAGE_FILE "EspFlashMemory.4m2m.dat"
#    endif

#    ifndef FLASH_MEMORY_STORAGE_MAX_SIZE
#        define FLASH_MEMORY_STORAGE_MAX_SIZE (4096 * 1024) // 4MByte
#    endif

#endif

#if _MSC_VER || ESP32

// linker address emulation
#    if _MSC_VER
#        include <eagle_soc.h>
#    endif

#    define EAGLE_SOC_ADDRESSchar(name)     extern "C" char *&name;
#    define EAGLE_SOC_ADDRESSuint32_t(name) extern "C" uint32_t &name;
#    define EAGLE_SOC_ADDRESSvoid(name)     extern "C" void &*name;
#    define EAGLE_SOC_ADDRESS(type, name)   EAGLE_SOC_ADDRESS##type(name)

#else

#    define EAGLE_SOC_ADDRESSchar(name)     extern "C" char name[];
#    define EAGLE_SOC_ADDRESSuint32_t(name) extern "C" uint32_t name;
#    define EAGLE_SOC_ADDRESSvoid(name)     extern "C" void *name;
#    define EAGLE_SOC_ADDRESS(type, name)   EAGLE_SOC_ADDRESS##type(name)

#endif

EAGLE_SOC_ADDRESS(uint32_t, _irom0_text_start);
EAGLE_SOC_ADDRESS(uint32_t, _irom0_text_end);
EAGLE_SOC_ADDRESS(char, _heap_start);
EAGLE_SOC_ADDRESS(uint32_t, _FS_start);
EAGLE_SOC_ADDRESS(uint32_t, _FS_end);
EAGLE_SOC_ADDRESS(uint32_t, _NVS_start);
EAGLE_SOC_ADDRESS(uint32_t, _NVS_end);
#if ESP32
    EAGLE_SOC_ADDRESS(uint32_t, _NVS2_start);
    EAGLE_SOC_ADDRESS(uint32_t, _NVS2_end);
#endif
EAGLE_SOC_ADDRESS(uint32_t, _SAVECRASH_start);
EAGLE_SOC_ADDRESS(uint32_t, _SAVECRASH_end);
EAGLE_SOC_ADDRESS(uint32_t, _EEPROM_start);
EAGLE_SOC_ADDRESS(uint32_t, _EEPROM_end);

#if ESP32

#    define SECTION_HEAP_START_ADDRESS       0x3ffae6e0U
#    define SECTION_HEAP_END_ADDRESS         0x3fffffffU
#    define SECTION_DRAM_START_ADDRESS       0x3ffae6e0U
#    define SECTION_DRAM_END_ADDRESS         0x3fffffffU
#    define SECTION_STACK_END_ADDRESS        0x3fffffffU

#else

#    define SECTION_HEAP_START_ADDRESS       ((uint32_t)&_heap_start[0])
#    define SECTION_HEAP_END_ADDRESS         0x3fffc000U
#    define SECTION_DRAM_START_ADDRESS       0x3ffe8000U
#    define SECTION_DRAM_END_ADDRESS         0x3fffffffU
#    define SECTION_STACK_END_ADDRESS        0x3fffffffU

#endif

// #endif
