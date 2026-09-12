#include "canvas.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// 5x7 font data
extern "C" {
#include "../third_party/glcdfont.c"
}

const uint8_t CC_RGB[4][3] = {
    {245, 242, 234},  // 0: CC_WHITE
    {26, 26, 26},     // 1: CC_BLACK
    {179, 32, 37},    // 2: CC_RED
    {232, 183, 26},   // 3: CC_YELLOW
};

CcCanvas g_canvas;

void CcCanvas::init(int width, int height) {
    w = width;
    h = height;
    px.assign(static_cast<size_t>(w) * h * 4, 255);
    fillRect(0, 0, w, h, CC_WHITE);
}

void CcCanvas::setPixel(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
    uint32_t idx = c & 3;
    p[0] = CC_RGB[idx][0];
    p[1] = CC_RGB[idx][1];
    p[2] = CC_RGB[idx][2];
    p[3] = 255;
}

uint32_t CcCanvas::getPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return CC_WHITE;
    const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
    for (uint32_t c = 0; c < 4; c++) {
        if (p[0] == CC_RGB[c][0] && p[1] == CC_RGB[c][1] && p[2] == CC_RGB[c][2])
            return c;
    }
    return CC_WHITE;
}

void CcCanvas::fillRect(int x, int y, int rw, int rh, uint32_t c) {
    if (rw <= 0 || rh <= 0) return;
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + rw > w ? w : x + rw;
    int y2 = y + rh > h ? h : y + rh;
    for (int yy = y1; yy < y2; yy++) {
        for (int xx = x1; xx < x2; xx++) {
            setPixel(xx, yy, c);
        }
    }
}

void CcCanvas::drawRect(int x, int y, int rw, int rh, uint32_t c) {
    if (rw <= 0 || rh <= 0) return;
    drawFastHLine(x, y, rw, c);
    drawFastHLine(x, y + rh - 1, rw, c);
    drawFastVLine(x, y, rh, c);
    drawFastVLine(x + rw - 1, y, rh, c);
}

void CcCanvas::drawFastHLine(int x, int y, int len, uint32_t c) {
    if (y < 0 || y >= h || len <= 0) return;
    int x1 = x < 0 ? 0 : x;
    int x2 = x + len > w ? w : x + len;
    for (int xx = x1; xx < x2; xx++) {
        setPixel(xx, y, c);
    }
}

void CcCanvas::drawFastVLine(int x, int y, int len, uint32_t c) {
    if (x < 0 || x >= w || len <= 0) return;
    int y1 = y < 0 ? 0 : y;
    int y2 = y + len > h ? h : y + len;
    for (int yy = y1; yy < y2; yy++) {
        setPixel(x, yy, c);
    }
}

void CcCanvas::drawLine(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (true) {
        setPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void CcCanvas::drawCircle(int x0, int y0, int r, uint32_t c) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    setPixel(x0, y0 + r, c);
    setPixel(x0, y0 - r, c);
    setPixel(x0 + r, y0, c);
    setPixel(x0 - r, y0, c);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        setPixel(x0 + x, y0 + y, c);
        setPixel(x0 - x, y0 + y, c);
        setPixel(x0 + x, y0 - y, c);
        setPixel(x0 - x, y0 - y, c);
        setPixel(x0 + y, y0 + x, c);
        setPixel(x0 - y, y0 + x, c);
        setPixel(x0 + y, y0 - x, c);
        setPixel(x0 - y, y0 - x, c);
    }
}

void CcCanvas::fillCircle(int x0, int y0, int r, uint32_t c) {
    drawFastVLine(x0, y0 - r, 2 * r + 1, c);
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawFastVLine(x0 + x, y0 - y, 2 * y + 1, c);
        drawFastVLine(x0 - x, y0 - y, 2 * y + 1, c);
        drawFastVLine(x0 + y, y0 - x, 2 * x + 1, c);
        drawFastVLine(x0 - y, y0 - x, 2 * x + 1, c);
    }
}

void CcCanvas::drawChar(int x, int y, unsigned char ch, uint32_t color, uint8_t size) {
    // ch is ASCII (0x00..0x7F) only — see the contract in canvas.h. UTF-8 decoding
    // happens in the shared decoder, and the degree symbol is drawn by the caller
    // as a vector circle, so there is exactly one decoder and one degree handler
    // in the project (no per-target non-ASCII fallback here).
    for (int i = 0; i < 5; i++) {
        uint8_t line = font[ch * 5 + i];
        for (int j = 0; j < 8; j++, line >>= 1) {
            if (line & 1) {
                if (size == 1) {
                    setPixel(x + i, y + j, color);
                } else {
                    fillRect(x + i * size, y + j * size, size, size, color);
                }
            }
        }
    }
}

bool CcCanvas::dumpPng(const char* path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    return stbi_write_png(path, w, h, 4, px.data(), w * 4) != 0;
}
