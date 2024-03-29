/**
  Author: sascha_lammers@gmx.de
*/

#include "section_defines.h"

// linker address emulation for partitions

#if ESP32

#include <Arduino_compat.h>
#include <esp_partition.h>

struct PartitionAddress {
    uint32_t *_start;
    uint32_t *_end;
    PartitionAddress(uint32_t start, uint32_t end) : _start((uint32_t *)start), _end((uint32_t *)end) {}
};

PartitionAddress get_partition_address_start(const char *label)
{
    auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part) {
        __DBG_panic("data partition label=%s not found", label);
    }
    __LDBG_printf_E("partition name=%s address=%08x end=%08x", part->address, (part->address + part->size - 1));
    return PartitionAddress(part->address + SECTION_FLASH_START_ADDRESS, part->address + part->size + (SECTION_FLASH_START_ADDRESS - 1));
}

// uint32_t &_irom0_text_start;
// uint32_t &_irom0_text_end;
// char &_heap_start[];
// uint32_t &_FS_start;
// uint32_t &_FS_end;
static auto P_NVS = get_partition_address_start("nvs");
static auto P_NVS2 = get_partition_address_start("nvs2");
static auto P_SAVECRASH = get_partition_address_start("savecrash");
static auto P_EEPROM = get_partition_address_start("eeprom");
static auto P_FS = get_partition_address_start("spiffs");

uint32_t &_NVS_start = *P_NVS._start;
uint32_t &_NVS_end = *P_NVS._end;
uint32_t &_NVS2_start = *P_NVS2._start;
uint32_t &_NVS2_end = *P_NVS2._end;
uint32_t &_SAVECRASH_start = *P_SAVECRASH._start;
uint32_t &_SAVECRASH_end = *P_SAVECRASH._end;
uint32_t &_EEPROM_start = *P_EEPROM._start;
uint32_t &_EEPROM_end = *P_EEPROM._end;
uint32_t &_FS_start = *P_FS._start;
uint32_t &_FS_end = *P_FS._end;

#endif

