// target_seeed.cpp — Device backend (Seeed GFX / ESP32-S3).
//
// Maps semantic colours (CC_*) to Seeed GFX values and draws proportional GFX glyphs
// through epaper.drawChar with setFreeFont, so the device path matches the host
// rasterizer pixel-for-pixel.

#include "draw/target.h"

#ifndef CHROMAWOTD_HOST
#include <Arduino.h>
#include "TFT_eSPI.h"

extern EPaper epaper;

namespace {
uint16_t toDeviceColor(uint32_t c) {
    switch (c) {
        case 0u /*CC_WHITE*/:  return TFT_WHITE;
        case 1u /*CC_BLACK*/:  return TFT_BLACK;
        case 2u /*CC_RED*/:    return TFT_RED;
        case 3u /*CC_YELLOW*/: return TFT_YELLOW;
        default:               return TFT_WHITE;
    }
}

class SeeedTarget : public DisplayTarget {
public:
    void fillRect(int x, int y, int w, int h, uint32_t c) override { epaper.fillRect(x, y, w, h, toDeviceColor(c)); }
    void drawRect(int x, int y, int w, int h, uint32_t c) override { epaper.drawRect(x, y, w, h, toDeviceColor(c)); }
    void drawFastHLine(int x, int y, int w, uint32_t c) override { epaper.drawFastHLine(x, y, w, toDeviceColor(c)); }
    void drawFastVLine(int x, int y, int h, uint32_t c) override { epaper.drawFastVLine(x, y, h, toDeviceColor(c)); }
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c) override { epaper.drawLine(x0, y0, x1, y1, toDeviceColor(c)); }
    void drawCircle(int x, int y, int r, uint32_t c) override { epaper.drawCircle(x, y, r, toDeviceColor(c)); }
    void fillCircle(int x, int y, int r, uint32_t c) override { epaper.fillCircle(x, y, r, toDeviceColor(c)); }

    void drawChar(int x, int y, unsigned char ch, uint32_t color, uint8_t size) override {
        epaper.setTextSize(size);
        epaper.setTextColor(toDeviceColor(color));
        epaper.drawChar(ch, x, y);
    }

    void drawGlyphF(const GFXfont* f, unsigned char ch, int x, int y, uint32_t c, int size) override {
        epaper.setTextSize(size);
        epaper.setTextColor(toDeviceColor(c));
        epaper.setFreeFont(f);
        epaper.drawChar(ch, x, y);
    }
};
}  // namespace

DisplayTarget& getSeeedTarget() {
    static SeeedTarget t;
    return t;
}

#endif  // !CHROMAWOTD_HOST
