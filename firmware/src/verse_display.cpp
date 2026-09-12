#include "verse_display.h"
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Shared UTF-8 -> single-byte decoding.
//
// TFT_eSPI's built-in font is ASCII/CP437 and must never receive multi-byte
// UTF-8: it would draw one garbage glyph per byte. Real API text (BibleGateway,
// Open-Meteo) carries curly quotes, en/em dashes, non-breaking spaces, ellipsis
// and warning signs, so every draw path funnels through cc_utf8ToAscii().
// 0xB0 is the degree sign sentinel and is drawn as a vector circle, not a glyph.
// ---------------------------------------------------------------------------
#define CC_DEGREE  0xB0
#define CC_UNKNOWN '?'
#define CC_GLYPH_W 6   // built-in font advance in pixels per size unit

// Maps the glyph starting at p to one ASCII byte. Returns bytes consumed (>= 1).
static int cc_utf8ToAscii(const unsigned char* p, unsigned char* out) {
    unsigned char c = p[0];
    if (c < 0x80) { *out = c; return 1; }

    // 2-byte sequences (U+0080..U+07FF)
    if (c == 0xC2 && p[1]) {
        if (p[1] == 0xB0) { *out = CC_DEGREE; return 2; }  // ° degree sign
        if (p[1] == 0xA0) { *out = ' ';       return 2; }  // non-breaking space
        *out = CC_UNKNOWN; return 2;
    }
    // 3-byte sequences (U+2000..U+2FFF)
    if (c == 0xE2 && p[1] && p[2]) {
        if (p[1] == 0x80) {
            switch (p[2]) {
                case 0x93: case 0x94: case 0x95: *out = '-';  return 3;  // – — ―
                case 0x98: case 0x99:            *out = '\''; return 3;  // ‘ ’
                case 0x9C: case 0x9D:            *out = '"';  return 3;  // “ ”
                case 0xA2:                       *out = '\''; return 3;  // ′ prime
                case 0xA6:                       *out = '.';  return 3;  // … ellipsis
                default: break;
            }
        }
        if (p[1] == 0x9A && p[2] == 0xA0) { *out = '!'; return 3; }  // ⚠ warning sign
        *out = CC_UNKNOWN; return 3;
    }
    // 4-byte sequences (emoji etc.) collapse to a single replacement glyph.
    if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { *out = CC_UNKNOWN; return 4; }
    *out = CC_UNKNOWN; return 1;
}

// Glyph (not byte) count of the first len bytes; mirrors cc_utf8ToAscii exactly
// so measured width always matches drawn width.
static int cc_countGlyphsN(const char* str, int len) {
    if (!str) return 0;
    int n = 0;
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    while (*p && p < end) { unsigned char g = 0; p += cc_utf8ToAscii(p, &g); n++; }
    return n;
}

static int cc_countGlyphs(const char* str) { return cc_countGlyphsN(str, (int)strlen(str)); }

// Round half away from zero: -0.6 -> -1 (plain (int)(t+0.5f) gives 0).
static int cc_roundTemp(float t) { return (int)lroundf(t); }

// Case-insensitive substring search (fallback for highlight phrases whose
// capitalisation is changed by the content source).
static const char* cc_findIgnoreCase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return nullptr;
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nlen) return p;
    }
    return nullptr;
}

bool verseHighlightFound(const VerseData& vd) {
    if (!vd.verse || !vd.highlight || !vd.highlight[0]) return false;
    return cc_findIgnoreCase(vd.verse, vd.highlight) != nullptr;
}

#ifdef CHROMAWOTD_HOST
#include "../test/harness/canvas.h"

static void dev_fillRect(int x, int y, int w, int h, uint32_t c) { g_canvas.fillRect(x, y, w, h, c); }
static void dev_drawRect(int x, int y, int w, int h, uint32_t c) { g_canvas.drawRect(x, y, w, h, c); }
static void dev_drawFastHLine(int x, int y, int w, uint32_t c) { g_canvas.drawFastHLine(x, y, w, c); }
static void dev_drawFastVLine(int x, int y, int h, uint32_t c) { g_canvas.drawFastVLine(x, y, h, c); }
static void dev_drawLine(int x0, int y0, int x1, int y1, uint32_t c) { g_canvas.drawLine(x0, y0, x1, y1, c); }
static void dev_drawCircle(int x, int y, int r, uint32_t c) { g_canvas.drawCircle(x, y, r, c); }
static void dev_fillCircle(int x, int y, int r, uint32_t c) { g_canvas.fillCircle(x, y, r, c); }

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    if (!str) return;
    int cursorX = x;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        g_canvas.drawChar(cursorX, y, glyph, c, (uint8_t)size);
        cursorX += CC_GLYPH_W * size;
    }
}
static int dev_measureText(const char* str, int size) { return cc_countGlyphs(str) * CC_GLYPH_W * size; }
static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) {
    dev_drawString(rx - dev_measureText(str, size), y, str, c, size);
}

#else
#include <Arduino.h>
#include "TFT_eSPI.h"

extern EPaper epaper;

static uint16_t toDeviceColor(uint32_t c) {
    switch (c) {
        case CC_WHITE:  return TFT_WHITE;
        case CC_BLACK:  return TFT_BLACK;
        case CC_RED:    return TFT_RED;
        case CC_YELLOW: return TFT_YELLOW;
        default:        return TFT_WHITE;
    }
}

static void dev_fillRect(int x, int y, int w, int h, uint32_t c) { epaper.fillRect(x, y, w, h, toDeviceColor(c)); }
static void dev_drawRect(int x, int y, int w, int h, uint32_t c) { epaper.drawRect(x, y, w, h, toDeviceColor(c)); }
static void dev_drawFastHLine(int x, int y, int w, uint32_t c) { epaper.drawFastHLine(x, y, w, toDeviceColor(c)); }
static void dev_drawFastVLine(int x, int y, int h, uint32_t c) { epaper.drawFastVLine(x, y, h, toDeviceColor(c)); }
static void dev_drawLine(int x0, int y0, int x1, int y1, uint32_t c) { epaper.drawLine(x0, y0, x1, y1, toDeviceColor(c)); }
static void dev_drawCircle(int x, int y, int r, uint32_t c) { epaper.drawCircle(x, y, r, toDeviceColor(c)); }
static void dev_fillCircle(int x, int y, int r, uint32_t c) { epaper.fillCircle(x, y, r, toDeviceColor(c)); }
static int dev_measureText(const char* str, int size) { return cc_countGlyphs(str) * CC_GLYPH_W * size; }

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    if (!str) return;
    epaper.setTextSize(size);
    epaper.setTextColor(toDeviceColor(c));
    int cursorX = x;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        if (glyph == CC_DEGREE) {
            epaper.drawCircle(cursorX + 2 * size, y + 2 * size, size, toDeviceColor(c));
        } else {
            epaper.drawChar(glyph, cursorX, y);
        }
        cursorX += CC_GLYPH_W * size;
    }
}

static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) {
    dev_drawString(rx - dev_measureText(str, size), y, str, c, size);
}
#endif

static void drawWeatherIcon(int cx, int cy, int size, int iconType, bool inverted = false) {
    int r = size / 3;
    if (r < 3) r = 3;

    uint32_t cloudOutline = inverted ? CC_WHITE : CC_BLACK;
    uint32_t cloudFill    = CC_WHITE;

    switch (iconType) {
        case 0: { // Sun
            dev_fillCircle(cx, cy, r, CC_YELLOW);
            dev_drawCircle(cx, cy, r, CC_YELLOW);
            // 8 Rays
            int r1 = r + 2;
            int r2 = size / 2;
            dev_drawLine(cx, cy - r1, cx, cy - r2, CC_YELLOW);
            dev_drawLine(cx, cy + r1, cx, cy + r2, CC_YELLOW);
            dev_drawLine(cx - r1, cy, cx - r2, cy, CC_YELLOW);
            dev_drawLine(cx + r1, cy, cx + r2, cy, CC_YELLOW);
            int d1 = (int)(r1 * 0.707f);
            int d2 = (int)(r2 * 0.707f);
            dev_drawLine(cx - d1, cy - d1, cx - d2, cy - d2, CC_YELLOW);
            dev_drawLine(cx + d1, cy - d1, cx + d2, cy - d2, CC_YELLOW);
            dev_drawLine(cx - d1, cy + d1, cx - d2, cy + d2, CC_YELLOW);
            dev_drawLine(cx + d1, cy + d1, cx + d2, cy + d2, CC_YELLOW);
            break;
        }
        case 1: { // Cloud
            dev_fillCircle(cx - size / 4, cy + size / 10, size / 5, cloudFill);
            dev_drawCircle(cx - size / 4, cy + size / 10, size / 5, cloudOutline);
            dev_fillCircle(cx + size / 5, cy + size / 10, size / 6, cloudFill);
            dev_drawCircle(cx + size / 5, cy + size / 10, size / 6, cloudOutline);
            dev_fillCircle(cx, cy - size / 10, size / 4, cloudFill);
            dev_drawCircle(cx, cy - size / 10, size / 4, cloudOutline);
            dev_fillRect(cx - size / 4, cy - size / 10, size / 2, size / 3, cloudFill);
            dev_drawFastHLine(cx - size / 3, cy + size / 4, (size * 2) / 3, cloudOutline);
            break;
        }
        case 2: { // Rain
            int cyShift = cy - 4;
            dev_fillCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudFill);
            dev_drawCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudOutline);
            dev_fillCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudFill);
            dev_drawCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudOutline);
            dev_fillCircle(cx, cyShift - size / 10, size / 4, cloudFill);
            dev_drawCircle(cx, cyShift - size / 10, size / 4, cloudOutline);
            dev_fillRect(cx - size / 4, cyShift - size / 10, size / 2, size / 3, cloudFill);
            dev_drawFastHLine(cx - size / 3, cyShift + size / 4, (size * 2) / 3, cloudOutline);
            // Red rain drops
            dev_drawLine(cx - 6, cyShift + size / 4 + 3, cx - 9, cyShift + size / 2 + 3, CC_RED);
            dev_drawLine(cx,     cyShift + size / 4 + 3, cx - 3, cyShift + size / 2 + 3, CC_RED);
            dev_drawLine(cx + 6, cyShift + size / 4 + 3, cx + 3, cyShift + size / 2 + 3, CC_RED);
            break;
        }
        case 3: // Partly cloudy
        default: {
            // Sun peeking behind cloud
            dev_fillCircle(cx - size / 4, cy - size / 5, size / 4, CC_YELLOW);
            dev_drawLine(cx - size / 4, cy - size / 5 - size / 4 - 3, cx - size / 4, cy - size / 5 - size / 4 - 1, CC_YELLOW);
            dev_drawLine(cx - size / 4 - size / 4 - 3, cy - size / 5, cx - size / 4 - size / 4 - 1, cy - size / 5, CC_YELLOW);
            // Masking cloud in foreground
            dev_fillCircle(cx - size / 6, cy + size / 8, size / 5, cloudFill);
            dev_drawCircle(cx - size / 6, cy + size / 8, size / 5, cloudOutline);
            dev_fillCircle(cx + size / 5, cy + size / 8, size / 6, cloudFill);
            dev_drawCircle(cx + size / 5, cy + size / 8, size / 6, cloudOutline);
            dev_fillCircle(cx + 2, cy - size / 12, size / 4, cloudFill);
            dev_drawCircle(cx + 2, cy - size / 12, size / 4, cloudOutline);
            dev_fillRect(cx - size / 6, cy - size / 12, (size * 5) / 12, size / 3, cloudFill);
            dev_drawFastHLine(cx - size / 4, cy + size / 4, (size * 7) / 12, cloudOutline);
            break;
        }
    }
}

// Lines the greedy wrapper below needs for a given width budget (mirrors the
// draw loops exactly, so the truncation decision is made before drawing).
static int cc_wrappedLineCount(const char* text, int maxW, int size) {
    if (!text || !*text) return 0;
    int charWidth = CC_GLYPH_W * size;
    int lines = 1, curX = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* wordStart = p;
        while (*p && *p != ' ') p++;
        int wordPx = cc_countGlyphsN(wordStart, (int)(p - wordStart)) * charWidth;
        if (curX > 0 && curX + wordPx > maxW) { lines++; curX = 0; }
        curX += wordPx + charWidth;
    }
    return lines;
}

// How many lines fit in maxH at this line height.
static int cc_lineCapacity(int maxH, int size, int lineHeight) {
    if (maxH < 8 * size) return 0;
    return (maxH - 8 * size) / lineHeight + 1;
}

// Width budget for one line. When the text will be cut off, the final line is kept
// short enough that the "..." marker still fits inline: 3 glyph widths of slack for
// a left-aligned block, 6 for a centred one (the line floats, so both margins count).
static int cc_lineBudget(int maxW, int charWidth, int slack, bool truncated, int lineIdx, int capacity) {
    if (truncated && capacity > 0 && lineIdx == capacity - 1) {
        int budget = maxW - slack * charWidth;
        if (budget >= 4 * charWidth) return budget;
    }
    return maxW;
}

// Marks text the block could not hold: "..." when the reserved space allows it,
// degrading to ".." rather than pushing characters off the block.
static void drawOverflowMarker(int x, int y, int maxRight, uint32_t color, int size) {
    int w = CC_GLYPH_W * size;
    if (x + 3 * w <= maxRight) { dev_drawString(x, y, "...", color, size); return; }
    if (x + 2 * w <= maxRight) { dev_drawString(x, y, "..", color, size); return; }
    dev_drawString(x, y, ".", color, size);
}

static int drawWrappedText(int startX, int startY, int maxW, int maxH, const char* text, uint32_t color, int size = 1, int lineHeight = 10) {
    if (!text || !*text) return startY;

    const int charWidth = CC_GLYPH_W * size;
    const int capacity = cc_lineCapacity(maxH, size, lineHeight);
    const bool truncated = cc_wrappedLineCount(text, maxW, size) > capacity;

    int curX = startX;
    int curY = startY;
    int lastX = startX;
    int lastY = startY;
    int lineIdx = 0;
    bool drewAny = false;

    const char* ptr = text;
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (!*ptr) break;

        if (curY + 8 * size > startY + maxH) break;

        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ') ptr++;
        int wordLen = (int)(ptr - wordStart);
        int wordPx = cc_countGlyphsN(wordStart, wordLen) * charWidth;
        int budget = cc_lineBudget(maxW, charWidth, 3, truncated, lineIdx, capacity);

        if (curX > startX && (curX + wordPx > startX + budget)) {
            curX = startX;
            curY += lineHeight;
            lineIdx++;
            if (curY + 8 * size > startY + maxH) break;
            budget = cc_lineBudget(maxW, charWidth, 3, truncated, lineIdx, capacity);
        }

        char wordBuf[64];
        int copyLen = wordLen < 63 ? wordLen : 63;
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        dev_drawString(curX, curY, wordBuf, color, size);
        curX += wordPx + charWidth;
        lastX = curX;
        lastY = curY;
        drewAny = true;
    }

    if (truncated && drewAny) drawOverflowMarker(lastX, lastY, startX + maxW, color, size);
    return curY + lineHeight;
}

static int drawWrappedTextCentered(int centerX, int startY, int maxW, int maxH, const char* text, uint32_t color, int size = 1, int lineHeight = 10) {
    if (!text || !*text) return startY;

    const int charWidth = CC_GLYPH_W * size;
    const int capacity = cc_lineCapacity(maxH, size, lineHeight);
    const bool truncated = cc_wrappedLineCount(text, maxW, size) > capacity;

    int curY = startY;
    int lineIdx = 0;
    int lastLineWidth = 0;
    int lastLineY = startY;

    const char* lineStart = text;
    while (*lineStart) {
        while (*lineStart == ' ') lineStart++;
        if (!*lineStart) break;

        if (curY + 8 * size > startY + maxH) break;

        int budget = cc_lineBudget(maxW, charWidth, 6, truncated, lineIdx, capacity);
        const char* p = lineStart;
        const char* lastWordEnd = lineStart;
        int linePx = 0;

        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            const char* wordStart = p;
            while (*p && *p != ' ') p++;
            int wordPx = cc_countGlyphsN(wordStart, (int)(p - wordStart)) * charWidth;
            int testPx = (linePx == 0) ? wordPx : (linePx + charWidth + wordPx);

            if (testPx > budget && linePx > 0) {
                break;
            }
            linePx = testPx;
            lastWordEnd = p;
        }

        // Render this line centered
        char lineBuf[96];
        int bytes = (int)(lastWordEnd - lineStart);
        if (bytes > 95) bytes = 95;
        memcpy(lineBuf, lineStart, bytes);
        lineBuf[bytes] = '\0';

        int lineWidth = dev_measureText(lineBuf, size);
        dev_drawString(centerX - lineWidth / 2, curY, lineBuf, color, size);

        lastLineWidth = lineWidth;
        lastLineY = curY;
        curY += lineHeight;
        lineIdx++;
        lineStart = lastWordEnd;
    }

    if (truncated && lastLineWidth > 0) {
        drawOverflowMarker(centerX + lastLineWidth / 2 + charWidth, lastLineY, centerX + maxW / 2, color, size);
    }

    return curY;
}



static void drawVerseBlock(int startX, int startY, int maxW, int maxH, const VerseData& vd, bool inverted = false) {
    if (!vd.verse) return;

    // Locate the highlighted phrase: exact match first, then case-insensitive so a
    // capitalisation change in the source does not silently drop the red accent.
    int hlStart = -1, hlEnd = -1;
    if (vd.highlight && vd.highlight[0]) {
        const char* p = strstr(vd.verse, vd.highlight);
        if (!p) p = cc_findIgnoreCase(vd.verse, vd.highlight);
        if (p) {
            hlStart = (int)(p - vd.verse);
            hlEnd = hlStart + (int)strlen(vd.highlight);
        }
    }

    int curX = startX;
    int curY = startY;
    int lastX = startX;
    int lastY = startY;
    int lineIdx = 0;
    bool drewAny = false;
    int lineHeight = 12;
    int charWidth = CC_GLYPH_W;
    const int capacity = cc_lineCapacity(maxH, 1, lineHeight);
    const bool truncated = cc_wrappedLineCount(vd.verse, maxW, 1) > capacity;

    const char* ptr = vd.verse;
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (!*ptr) break;

        if (curY + 8 > startY + maxH) break;

        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ') ptr++;
        int wordLen = (int)(ptr - wordStart);
        int wordPx = cc_countGlyphsN(wordStart, wordLen) * charWidth;
        int wordIdx = (int)(wordStart - vd.verse);
        int budget = cc_lineBudget(maxW, charWidth, 4, truncated, lineIdx, capacity);

        if (curX > startX && (curX + wordPx > startX + budget)) {
            curX = startX;
            curY += lineHeight;
            lineIdx++;
            if (curY + 8 > startY + maxH) break;
            budget = cc_lineBudget(maxW, charWidth, 4, truncated, lineIdx, capacity);
        }

        // A word is highlighted when it overlaps the phrase range at all — not
        // merely when the phrase starts inside it.
        bool isHl = (hlStart >= 0 && wordIdx < hlEnd && wordIdx + wordLen > hlStart);
        uint32_t wordColor = isHl ? (inverted ? CC_YELLOW : CC_RED) : (inverted ? CC_WHITE : CC_BLACK);

        char wordBuf[64];
        int copyLen = wordLen < 63 ? wordLen : 63;
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        dev_drawString(curX, curY, wordBuf, wordColor, 1);
        curX += wordPx + charWidth;
        lastX = curX;
        lastY = curY;
        drewAny = true;
    }

    if (truncated && drewAny) {
        drawOverflowMarker(lastX, lastY, startX + maxW, inverted ? CC_WHITE : CC_BLACK, 1);
    }
}

void drawLayoutPortrait(const VerseData& v, const WeatherData& w) {
    // 1. Header (Yellow band)
    dev_fillRect(0, 0, 128, 22, CC_YELLOW);
    dev_drawString(4, 3, "DAILY VERSE", CC_BLACK, 1);
    if (v.date) {
        dev_drawStringRight(124, 12, v.date, CC_BLACK, 1);
    }
    dev_drawFastHLine(0, 21, 128, CC_BLACK);

    // 2. Verse body (White background)
    dev_fillRect(0, 22, 128, 222, CC_WHITE);
    drawVerseBlock(6, 28, 116, 172, v);

    // 3. Reference line (Red)
    if (v.reference) {
        dev_drawFastHLine(14, 204, 100, CC_RED);
        dev_drawStringRight(122, 210, v.reference, CC_RED, 1);
    }

    // 4. Weather strip
    dev_fillRect(0, 244, 128, 52, CC_WHITE);
    dev_drawFastHLine(0, 244, 128, CC_BLACK);

    // Forecast label
    dev_drawString(4, 247, "FORECAST", CC_BLACK, 1);

    drawWeatherIcon(22, 276, 26, w.icon);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));
    dev_drawString(44, 258, tbuf, CC_BLACK, 2);

    int curY = 274;
    if (w.condition) {
        int availH = w.alert ? 10 : 20;
        curY = drawWrappedText(44, curY, 80, availH, w.condition, CC_BLACK, 1, 9);
    }

    if (w.alert) {
        if (curY < 284) curY = 284;
        drawWrappedText(44, curY, 80, 296 - curY, w.alert, CC_RED, 1, 9);
    }
}

void drawLayoutPortraitInverted(const VerseData& v, const WeatherData& w) {
    // Canvas: Full Black background
    dev_fillRect(0, 0, 128, 296, CC_BLACK);

    // 1. Header (Yellow band)
    dev_fillRect(0, 0, 128, 22, CC_YELLOW);
    dev_drawString(4, 3, "DAILY VERSE", CC_BLACK, 1);
    if (v.date) {
        dev_drawStringRight(124, 12, v.date, CC_BLACK, 1);
    }
    dev_drawFastHLine(0, 21, 128, CC_BLACK);

    // 2. Verse body (Black background, white text, yellow highlight)
    drawVerseBlock(6, 28, 116, 172, v, true);

    // 3. Reference line (Red)
    if (v.reference) {
        dev_drawFastHLine(14, 204, 100, CC_RED);
        dev_drawStringRight(122, 210, v.reference, CC_RED, 1);
    }

    // 4. Weather strip
    dev_drawFastHLine(0, 244, 128, CC_WHITE);

    // Forecast label
    dev_drawString(4, 247, "FORECAST", CC_YELLOW, 1);

    drawWeatherIcon(22, 276, 26, w.icon, true);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));
    dev_drawString(44, 258, tbuf, CC_WHITE, 2);

    int curY = 274;
    if (w.condition) {
        int availH = w.alert ? 10 : 20;
        curY = drawWrappedText(44, curY, 80, availH, w.condition, CC_WHITE, 1, 9);
    }

    if (w.alert) {
        if (curY < 284) curY = 284;
        drawWrappedText(44, curY, 80, 296 - curY, w.alert, CC_RED, 1, 9);
    }
}

void drawLayoutLandscape(const VerseData& v, const WeatherData& w) {
    const int splitX = 205;

    // 1. Header (Yellow band)
    dev_fillRect(0, 0, splitX, 20, CC_YELLOW);
    dev_drawString(6, 6, "Verse of the Day", CC_BLACK, 1);
    if (v.date) {
        dev_drawStringRight(splitX - 6, 6, v.date, CC_BLACK, 1);
    }
    dev_drawFastHLine(0, 19, splitX, CC_BLACK);

    // 2. Verse body (White background)
    dev_fillRect(0, 20, splitX, 108, CC_WHITE);
    drawVerseBlock(8, 26, 188, 76, v);

    // 3. Reference line (Red)
    if (v.reference) {
        dev_drawFastHLine(10, 104, splitX - 20, CC_RED);
        dev_drawStringRight(splitX - 8, 110, v.reference, CC_RED, 1);
    }

    // 4. Vertical divider
    dev_drawFastVLine(splitX, 0, 128, CC_BLACK);

    // 5. Right-hand weather column (x = 206..295, w = 90)
    dev_fillRect(splitX + 1, 0, 296 - splitX - 1, 128, CC_WHITE);

    // Forecast header (Centered at x = 251)
    int fcWidth = dev_measureText("FORECAST", 1);
    dev_drawString(251 - fcWidth / 2, 6, "FORECAST", CC_BLACK, 1);
    dev_drawFastHLine(215, 17, 72, CC_BLACK);

    // Weather icon moved down 1 pixel (cy = 32)
    drawWeatherIcon(251, 32, 24, w.icon);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));
    int tWidth = dev_measureText(tbuf, 2);
    dev_drawString(251 - tWidth / 2, 47, tbuf, CC_BLACK, 2);

    int curY = 65;
    if (w.condition) {
        int availH = w.alert ? 20 : 58;
        curY = drawWrappedTextCentered(251, curY, 84, availH, w.condition, CC_BLACK, 1, 10);
    }

    if (w.alert) {
        if (curY < 78) curY = 78;
        dev_drawFastHLine(215, curY, 72, CC_RED);
        int alWidth = dev_measureText("ALERT:", 1);
        dev_drawString(251 - alWidth / 2, curY + 3, "ALERT:", CC_RED, 1);
        drawWrappedTextCentered(251, curY + 14, 84, 128 - (curY + 14), w.alert, CC_RED, 1, 10);
    }
}

void drawLayoutLandscapeInverted(const VerseData& v, const WeatherData& w) {
    const int splitX = 205;

    // Fill entire canvas black
    dev_fillRect(0, 0, 296, 128, CC_BLACK);

    // 1. Header (Yellow band)
    dev_fillRect(0, 0, splitX, 20, CC_YELLOW);
    dev_drawString(6, 6, "Verse of the Day", CC_BLACK, 1);
    if (v.date) {
        dev_drawStringRight(splitX - 6, 6, v.date, CC_BLACK, 1);
    }
    dev_drawFastHLine(0, 19, splitX, CC_BLACK);

    // 2. Verse body (Black background, white text, yellow highlight)
    drawVerseBlock(8, 26, 188, 76, v, true);

    // 3. Reference line (Red)
    if (v.reference) {
        dev_drawFastHLine(10, 104, splitX - 20, CC_RED);
        dev_drawStringRight(splitX - 8, 110, v.reference, CC_RED, 1);
    }

    // 4. Vertical divider (White)
    dev_drawFastVLine(splitX, 0, 128, CC_WHITE);

    // 5. Right-hand weather column (Black background)
    // Forecast header (Centered at x = 251)
    int fcWidth = dev_measureText("FORECAST", 1);
    dev_drawString(251 - fcWidth / 2, 6, "FORECAST", CC_YELLOW, 1);
    dev_drawFastHLine(215, 17, 72, CC_YELLOW);

    // Weather icon moved down 1 pixel (cy = 32)
    drawWeatherIcon(251, 32, 24, w.icon, true);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));
    int tWidth = dev_measureText(tbuf, 2);
    dev_drawString(251 - tWidth / 2, 47, tbuf, CC_WHITE, 2);

    int curY = 65;
    if (w.condition) {
        int availH = w.alert ? 20 : 58;
        curY = drawWrappedTextCentered(251, curY, 84, availH, w.condition, CC_WHITE, 1, 10);
    }

    if (w.alert) {
        if (curY < 78) curY = 78;
        dev_drawFastHLine(215, curY, 72, CC_RED);
        int alWidth = dev_measureText("ALERT:", 1);
        dev_drawString(251 - alWidth / 2, curY + 3, "ALERT:", CC_RED, 1);
        drawWrappedTextCentered(251, curY + 14, 84, 128 - (curY + 14), w.alert, CC_RED, 1, 10);
    }
}

void drawLayoutLandscapeDark(const VerseData& v, const WeatherData& w) {
    const int splitX = 205;

    // Full midnight black canvas
    dev_fillRect(0, 0, 296, 128, CC_BLACK);

    // 1. Header (Dark: Yellow text, white date, yellow separator line)
    dev_drawString(6, 6, "Verse of the Day", CC_YELLOW, 1);
    if (v.date) {
        dev_drawStringRight(splitX - 6, 6, v.date, CC_WHITE, 1);
    }
    dev_drawFastHLine(0, 19, splitX, CC_YELLOW);

    // 2. Verse body (Black background, white text, yellow highlight)
    drawVerseBlock(8, 26, 188, 76, v, true);

    // 3. Reference line (Red)
    if (v.reference) {
        dev_drawFastHLine(10, 104, splitX - 20, CC_RED);
        dev_drawStringRight(splitX - 8, 110, v.reference, CC_RED, 1);
    }

    // 4. Vertical divider (Yellow)
    dev_drawFastVLine(splitX, 0, 128, CC_YELLOW);

    // 5. Right-hand weather column (Black background)
    // Forecast header (Centered at x = 251)
    int fcWidth = dev_measureText("FORECAST", 1);
    dev_drawString(251 - fcWidth / 2, 6, "FORECAST", CC_YELLOW, 1);
    dev_drawFastHLine(215, 17, 72, CC_YELLOW);

    // Weather icon moved down 1 pixel (cy = 32)
    drawWeatherIcon(251, 32, 24, w.icon, true);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));
    int tWidth = dev_measureText(tbuf, 2);
    dev_drawString(251 - tWidth / 2, 47, tbuf, CC_WHITE, 2);

    int curY = 65;
    if (w.condition) {
        int availH = w.alert ? 20 : 58;
        curY = drawWrappedTextCentered(251, curY, 84, availH, w.condition, CC_WHITE, 1, 10);
    }

    if (w.alert) {
        if (curY < 78) curY = 78;
        dev_drawFastHLine(215, curY, 72, CC_RED);
        int alWidth = dev_measureText("ALERT:", 1);
        dev_drawString(251 - alWidth / 2, curY + 3, "ALERT:", CC_RED, 1);
        drawWrappedTextCentered(251, curY + 14, 84, 128 - (curY + 14), w.alert, CC_RED, 1, 10);
    }
}

void drawLayout(const VerseData& v, const WeatherData& w, bool landscape, bool inverted) {
    if (landscape) {
        if (inverted) {
            drawLayoutLandscapeInverted(v, w);
        } else {
            drawLayoutLandscape(v, w);
        }
    } else {
        if (inverted) {
            drawLayoutPortraitInverted(v, w);
        } else {
            drawLayoutPortrait(v, w);
        }
    }
}


