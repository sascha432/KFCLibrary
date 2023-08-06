/**
 * Author: sascha_lammers@gmx.de
 */

//
// allow to use IRAM as heap if available
//
// using the SELECT_IRAM/DRAM() macros switches the heap for the current scope only
//

#pragma once

#if ESP8266
#    include "umm_malloc/umm_malloc_cfg.h"
#    include <umm_malloc/umm_heap_select.h>
#    define HAS_MULTI_HEAP defined(UMM_NUM_HEAPS) && (UMM_NUM_HEAPS > 1)
#    if defined(MMU_IRAM_HEAP)
#        define SELECT_IRAM() HeapSelectIram ephemeral;
#        define SELECT_DRAM() HeapSelectDram ephemeral;
#    else
#        define SELECT_IRAM() ;
#        define SELECT_DRAM() ;
#    endif
#else
#    define HAS_MULTI_HEAP 0
#    define SELECT_IRAM() ;
#    define SELECT_DRAM() ;
#endif

// return current heap type
inline const __FlashStringHelper *getCurrentHeapType()
{
    #if HAS_MULTI_HEAP
        if (umm_get_current_heap_id() == UMM_HEAP_IRAM) {
            return F("IRAM");
        }
    #endif
    return F("DRAM");
}

inline uint32_t getTotalFreeHeap()
{
    #if ESP32
        return ESP.getFreeHeap();
    #else
        uint32_t freeHeap;
        {
            SELECT_DRAM();
            freeHeap = ESP.getFreeHeap();
        }
        #if HAS_MULTI_HEAP
            {
                SELECT_IRAM();
                freeHeap += ESP.getFreeHeap();
            }
        #endif
        return freeHeap;
    #endif
}
