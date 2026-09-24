/**
  Author: sascha_lammers@gmx.de
*/

#include <Arduino_compat.h>
#include "StrView.h"

#if STRVIEW_ENABLE_VALIDATION

// 8 and 16 bit reads from PROGMEM are emulated by the core (exception 3/LoadStoreError) and 32
// bit reads from PROGMEM require an aligned address. every pointer that is neither RAM/stack nor
// PROGMEM must not be read at all - this reports pointers to String objects, IPAddress objects,
// ROM memory, ... before they are used
//
// this is the only pointer check in StrView and it is DEBUG only, the release build does not look
// at the pointer or the storage type at all

void StrView::_validate(const char *ptr)
{
    __DBG_assertf(ptr == nullptr || is_HEAP_P(ptr) || is_PGM_P(ptr), "StrView(%p): invalid pointer, RAM/stack or PROGMEM required", ptr);
}

#endif
