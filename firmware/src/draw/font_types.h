// font_types.h — GFXfont/GFXglyph type definitions shared by the layout engine
// and both display backends.
//
// On device these come from Seeed GFX's gfxfont.h (via TFT_eSPI.h). On host there
// is no gfxfont.h, so we provide a layout-compatible shim. Keeping this in one
// header (instead of a per-TU #ifdef block) is what lets the DisplayTarget
// interface reference GFXfont without entangling every target in the FreeSans
// glyph path.

#pragma once
#include <cstdint>

#ifdef CHROMAWOTD_HOST
// Host shim: no gfxfont.h available. These structs mirror the device layout.
#ifndef PROGMEM
#define PROGMEM
#endif
typedef struct { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; } GFXglyph;
typedef struct { const uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; } GFXfont;
#else
// Device: GFXglyph/GFXfont/PROGMEM come from TFT_eSPI.h -> gfxfont.h.
#include "TFT_eSPI.h"
#endif
