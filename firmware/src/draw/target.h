// target.h — DisplayTarget interface for CHROMAWOTD.
//
// The layout engine (verse_display.cpp) draws through this interface rather than
// calling Seeed GFX or CcCanvas directly. Two implementations exist:
//   - CanvasTarget (host):   mock canvas + PNG export (target_canvas.cpp)
//   - SeeedTarget  (device): Seeed GFX / ESP32-S3 (target_seeed.cpp)
//
// Adding a third target (framebuffer, desktop window) means implementing this
// interface — no duplicated #ifdef forwarding, no third copy of the degree and
// colour-mapping special-casing.

#pragma once

#include <cstdint>
#include "font_types.h"

// The two backend singletons. Each is defined in its own translation unit and
// compiled only when its target is active (guarded by CHROMAWOTD_HOST).
class DisplayTarget;
DisplayTarget& getCanvasTarget();   // host  (defined in target_canvas.cpp)
DisplayTarget& getSeeedTarget();    // device (defined in target_seeed.cpp)

class DisplayTarget {
public:
    virtual ~DisplayTarget() = default;

    // --- shape primitives (colour mapping happens inside each target) ---
    virtual void fillRect(int x, int y, int w, int h, uint32_t c) = 0;
    virtual void drawRect(int x, int y, int w, int h, uint32_t c) = 0;
    virtual void drawFastHLine(int x, int y, int w, uint32_t c) = 0;
    virtual void drawFastVLine(int x, int y, int h, uint32_t c) = 0;
    virtual void drawLine(int x0, int y0, int x1, int y1, uint32_t c) = 0;
    virtual void drawCircle(int x, int y, int r, uint32_t c) = 0;
    virtual void fillCircle(int x, int y, int r, uint32_t c) = 0;

    // --- text primitives ---
    // Draw one ASCII glyph (0x00..0x7F) from the built-in 5x7 font at (x, y),
    // 6px advance, magnification `size`. The degree symbol is NOT handled here —
    // the caller draws it as a vector circle (see dev_drawDegree), so degree +
    // colour mapping stay single-purpose per target instead of entangled in a
    // string loop.
    virtual void drawChar(int x, int y, unsigned char ch, uint32_t color, uint8_t size) = 0;

    // Draw one glyph from a GFXfont (FreeSans/Roboto) at baseline (x, y),
    // magnification `size`, honouring xOffset/yOffset so descenders hang below
    // the baseline. The host rasterizes the packed bitmap; the device uses
    // setFreeFont + drawChar.
    virtual void drawGlyphF(const GFXfont* f, unsigned char ch, int x, int y, uint32_t color, int size) = 0;
};
