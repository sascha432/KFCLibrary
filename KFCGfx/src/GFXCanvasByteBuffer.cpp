/**
* Author: sascha_lammers@gmx.de
*/

#if HAVE_KFCGFXLIB

#include <Arduino_compat.h>
#include "GFXCanvasConfig.h"

#pragma GCC push_options
#if DEBUG_GFXCANVAS
#include <debug_helper_enable.h>
#else
#include <debug_helper_disable.h>
#pragma GCC optimize ("O3")
#endif

#include "GFXCanvasByteBuffer.h"

using namespace GFXCanvas;


#pragma GCC pop_options

#endif
