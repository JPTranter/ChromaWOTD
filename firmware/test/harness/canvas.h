#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Semantic ePaper colors (matching firmware constants)
#define CC_WHITE   0u
#define CC_BLACK   1u
#define CC_RED     2u
#define CC_YELLOW  3u

// RGB approximations of the four ePaper pigments
extern const uint8_t CC_RGB[4][3];

// Mock canvas: receives only ASCII bytes plus the 0xB0 degree sentinel. UTF-8
// decoding happens in the shared decoder (verse_display.cpp's cc_utf8ToAscii)
// before any draw call, so there is exactly one decoder in the project — the
// canvas never re-decodes multi-byte text.
struct CcCanvas {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;  // RGBA buffer

    void init(int width, int height);
    void setPixel(int x, int y, uint32_t c);
    uint32_t getPixel(int x, int y) const;
    void fillRect(int x, int y, int rw, int rh, uint32_t c);
    void drawRect(int x, int y, int rw, int rh, uint32_t c);
    void drawFastHLine(int x, int y, int len, uint32_t c);
    void drawFastVLine(int x, int y, int len, uint32_t c);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c);
    void drawCircle(int x0, int y0, int r, uint32_t c);
    void fillCircle(int x0, int y0, int r, uint32_t c);

    // ch is ASCII (0x00..0x7F) or the 0xB0 degree sentinel; see the contract above.
    void drawChar(int x, int y, unsigned char ch, uint32_t color, uint8_t size = 1);

    bool dumpPng(const char* path);
};

// Global mock canvas instance for host testing
extern CcCanvas g_canvas;
