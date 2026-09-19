// target_canvas.cpp — Host backend (mock canvas + PNG export).
//
// The canvas receives only ASCII bytes (0x00..0x7F); UTF-8 decoding and the degree
// symbol are handled by the shared layout code before any draw call reaches here.
// Colour mapping is identity (CC_* == canvas indices).

#include "draw/target.h"

#ifdef CHROMAWOTD_HOST
#include "canvas.h"

namespace {
class CanvasTarget : public DisplayTarget {
public:
    void fillRect(int x, int y, int w, int h, uint32_t c) override { g_canvas.fillRect(x, y, w, h, c); }
    void drawRect(int x, int y, int w, int h, uint32_t c) override { g_canvas.drawRect(x, y, w, h, c); }
    void drawFastHLine(int x, int y, int w, uint32_t c) override { g_canvas.drawFastHLine(x, y, w, c); }
    void drawFastVLine(int x, int y, int h, uint32_t c) override { g_canvas.drawFastVLine(x, y, h, c); }
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c) override { g_canvas.drawLine(x0, y0, x1, y1, c); }
    void drawCircle(int x, int y, int r, uint32_t c) override { g_canvas.drawCircle(x, y, r, c); }
    void fillCircle(int x, int y, int r, uint32_t c) override { g_canvas.fillCircle(x, y, r, c); }

    void drawChar(int x, int y, unsigned char ch, uint32_t color, uint8_t size) override {
        g_canvas.drawChar(x, y, ch, color, size);
    }

    // GFX-font glyph rasterizer: decode the packed MSB-first bitstream and paint
    // each set bit as a size×size block at (x + xOffset, y + yOffset), matching
    // TFT_eSPI's free-font drawChar exactly.
    void drawGlyphF(const GFXfont* f, unsigned char ch, int x, int y, uint32_t c, int size) override {
        if (ch < f->first || ch > f->last) { g_canvas.drawChar(x, y, '?', c, (uint8_t)size); return; }
        const GFXglyph* g = &f->glyph[ch - f->first];
        if (!g->width || !g->height) return;   // space etc.
        int nbit = 0;
        for (int r = 0; r < g->height; r++) {
            for (int col = 0; col < g->width; col++) {
                int byteaddr = g->bitmapOffset + nbit / 8;
                uint8_t byte = f->bitmap[byteaddr];
                if (byte & (0x80 >> (nbit & 7))) {
                    int px = x + g->xOffset * size + col * size;
                    int py = y + g->yOffset * size + r * size;
                    for (int dy = 0; dy < size; dy++)
                        for (int dx = 0; dx < size; dx++)
                            g_canvas.setPixel(px + dx, py + dy, c);
                }
                nbit++;
            }
        }
    }
};
}  // namespace

DisplayTarget& getCanvasTarget() {
    static CanvasTarget t;
    return t;
}

#endif  // CHROMAWOTD_HOST
