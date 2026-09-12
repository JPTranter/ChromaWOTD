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

static int cc_utf8ToAscii(const unsigned char* p, unsigned char* out);

// ---------------------------------------------------------------------------
// Font metrics abstraction.
//
// The whole layout engine measures text in pixel widths assuming a CONSTANT
// per-glyph advance (CC_GLYPH_W per size unit). That is true of the built-in
// 5x7 font but NOT of a proportional GFX font. Under CHROMAWOTD_FONT_FREESANS
// the FreeSans 8pt GFX font is used: every glyph carries its own xAdvance and
// the line height is the font's yAdvance. All measurement and line-stepping
// below routes through cc_advance()/cc_measurePx()/cc_lineHeightPs() so the
// decision is localised here. Default (no flag) is unchanged: 6px * size.
// ---------------------------------------------------------------------------
#ifdef CHROMAWOTD_FONT_FREESANS
#ifdef CHROMAWOTD_HOST
// Host has no gfxfont.h (device lib). Provide the two structs + extern the font.
#ifndef PROGMEM
#define PROGMEM
#endif
typedef struct { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; } GFXglyph;
typedef struct { const uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; } GFXfont;
#else
// Device: GFXglyph/GFXfont/PROGMEM come from TFT_eSPI.h -> gfxfont.h. Include it
// here (early) so the font header below and the metrics helpers can use them.
// verse_display.cpp is its own TU; main.cpp's include of TFT_eSPI.cpp does not
// carry over. Board macros are supplied as global build_flags.
#include "TFT_eSPI.h"
#endif
#include "fonts/Roboto55pt7b.h"    // body font (needs GFXglyph/GFXfont visible)
#include "fonts/Roboto5pt7b.h"     // auto-size small body (long verses)
#include "fonts/Roboto6pt7b.h"     // auto-size large body (short verses)
#include "fonts/RobotoT10pt7b.h"   // temperature font (dedicated, native-size)
#endif

// Active body font. The main UI defaults to 5.5pt; the verse block auto-sizes
// (5 / 5.5 / 6pt) to the available box by switching this pointer around the
// verse draw only, then restoring. Alert text switches to 5pt specifically.
#ifdef CHROMAWOTD_FONT_FREESANS
static const GFXfont* g_bodyFont = &Roboto55pt7b;
static const GFXfont* cc_setBodyFont(const GFXfont* f) {
    const GFXfont* prev = g_bodyFont;
    g_bodyFont = f;
    return prev;
}
static void cc_restoreBodyFont(const GFXfont* prev) { g_bodyFont = prev; }
#endif

// Font ascent (glyph_ab in TFT_eSPI): largest distance from baseline up to a
// glyph top. TFT_eSPI's drawString() with a free font does poY += glyph_ab
// internally, then draws each glyph at (baseline + yOffset) -- so y passed to
// drawString() is the glyph TOP and caps land exactly at y. The host mock below
// replicates that exactly so the preview matches the device pixel-for-pixel.
#ifdef CHROMAWOTD_FONT_FREESANS
static int cc_glyphAscentF(const GFXfont* f, int size) {
    int ab = 0;
    for (int c = f->first; c <= f->last; c++) {
        const GFXglyph* g = &f->glyph[c - f->first];
        if (g->width && g->height) {
            int a = -g->yOffset;             // this glyph's ascent
            if (a > ab) ab = a;
        }
    }
    return ab * size;
}
#endif
static int cc_glyphAscent(int size) {
#ifdef CHROMAWOTD_FONT_FREESANS
    return cc_glyphAscentF(g_bodyFont, size);
#else
    return 0;
#endif
}

// Forward-declared: each backend (host canvas / device epaper) provides its own
// primitive below, after this shared section.
static void dev_drawCircle(int x, int y, int r, uint32_t c);

// Pixel advance of ONE decoded ASCII glyph at the given magnification.
#ifdef CHROMAWOTD_FONT_FREESANS
static int cc_advanceF(const GFXfont* f, unsigned char ascii, int size) {
    if (ascii >= f->first && ascii <= f->last) {
        GFXglyph* g = &f->glyph[ascii - f->first];
        return (g->width || g->height) ? g->xAdvance * size : 6 * size;
    }
    return 6 * size;
}
#endif
static int cc_advance(unsigned char ascii, int size) {
#ifdef CHROMAWOTD_FONT_FREESANS
    return cc_advanceF(g_bodyFont, ascii, size);
#else
    return CC_GLYPH_W * size;
#endif
}

// Degree-sign geometry for a given font. The degree is a small superscript
// circle sitting at the TOP of the digits, not a baseline character. Center it
// so its top roughly aligns with the font's cap top, and size it relative to
// the font's ascent so it reads correctly across body and temperature fonts.
#ifdef CHROMAWOTD_FONT_FREESANS
static int cc_degreeRadius(const GFXfont* f) {
    int r = cc_glyphAscentF(f, 1) / 4;   // ~3 for the 10pt temp, ~2 for 5.5pt body
    if (r < 1) r = 1;
    if (r > 3) r = 3;
    return r;
}

// Draw a degree circle for a given font: a small superscript circle whose top
// aligns near the font's cap top and whose left sits at the cursor+radius.
// Shared by host + device (both provide drawCircle).
static void dev_drawDegree(const GFXfont* f, int cursorX, int base, uint32_t c, int size) {
    int r = cc_degreeRadius(f) * size;
    int cx = cursorX + r;
    int cy = base - cc_glyphAscentF(f, size) + r;   // top near cap top
    dev_drawCircle(cx, cy, r, c);
}
#endif   // CHROMAWOTD_FONT_FREESANS

// Pixel width of a decoded string at the given magnification.
static int cc_measurePx(const char* str, int size) {
    if (!str) return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) { unsigned char g = 0; p += cc_utf8ToAscii(p, &g); w += cc_advance(g, size); }
    return w;
}

// Pixel width of a decoded string in a specific GFX font (temperature etc.).
#ifdef CHROMAWOTD_FONT_FREESANS
static int cc_measurePxF(const GFXfont* f, const char* str, int size) {
    if (!str) return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) { unsigned char g = 0; p += cc_utf8ToAscii(p, &g); w += cc_advanceF(f, g, size); }
    return w;
}
#endif   // CHROMAWOTD_FONT_FREESANS

// Same, but for a substring of `len` bytes (word-splitting callers).
static int cc_measurePxN(const char* str, int len, int size) {
    if (!str || len <= 0) return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    while (*p && p < end) { unsigned char g = 0; p += cc_utf8ToAscii(p, &g); w += cc_advance(g, size); }
    return w;
}

// Vertical distance between successive text baselines.
static int cc_lineHeight(int size, int fallback) {
#ifdef CHROMAWOTD_FONT_FREESANS
    return g_bodyFont->yAdvance * size;
#else
    return fallback;
#endif
}

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

#ifdef CHROMAWOTD_FONT_FREESANS
// Render one glyph from a specific free font at baseline (x,y), magnification
// size, honouring xOffset/yOffset so descenders hang below the baseline.
static void dev_drawGlyphF(const GFXfont* f, unsigned char ch, int x, int y, uint32_t c, int size) {
    if (ch < f->first || ch > f->last) { g_canvas.drawChar(x, y, '?', c, (uint8_t)size); return; }
    const GFXglyph* g = &f->glyph[ch - f->first];
    if (!g->width || !g->height) return;   // space etc.
    // decode packed bitmap: byte offset g->bitmapOffset, MSB-first bitstream
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

static void dev_drawGlyph(unsigned char ch, int x, int y, uint32_t c, int size) {
    dev_drawGlyphF(g_bodyFont, ch, x, y, c, size);
}

static void dev_drawStringF(const GFXfont* f, int x, int y, const char* str, uint32_t c, int size) {
    if (!str) return;
    int cursorX = x;
    int base = y + cc_glyphAscentF(f, size);   // replicate TFT_eSPI poY += glyph_ab
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        if (glyph == CC_DEGREE) {
            dev_drawDegree(f, cursorX, base, c, size);
        } else {
            dev_drawGlyphF(f, glyph, cursorX, base, c, size);
        }
        cursorX += cc_advanceF(f, glyph, size);
    }
}

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    dev_drawStringF(g_bodyFont, x, y, str, c, size);
}
#else
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
#endif
static int dev_measureText(const char* str, int size) { return cc_measurePx(str, size); }
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
static int dev_measureText(const char* str, int size) { return cc_measurePx(str, size); }

static void dev_drawStringF(const GFXfont* f, int x, int y, const char* str, uint32_t c, int size) {
    if (!str) return;
#ifdef CHROMAWOTD_FONT_FREESANS
    // Per-glyph so we can special-case the degree (0xB0) which the ASCII-only
    // GFX font does NOT contain -- drawString() would silently drop it. Mirrors
    // the host path: baseline = y + glyphAscent, glyph drawn at baseline+yOffset.
    epaper.setTextColor(toDeviceColor(c));
    epaper.setTextSize(size);
    epaper.setFreeFont(f);
    int baseline = y + cc_glyphAscentF(f, size);
    int cursorX = x;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        if (glyph == CC_DEGREE) {
            dev_drawDegree(f, cursorX, baseline, c, size);
        } else {
            epaper.drawChar(glyph, cursorX, baseline);
        }
        cursorX += cc_advanceF(f, glyph, size);
    }
    return;
#else
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
#endif
}

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    dev_drawStringF(g_bodyFont, x, y, str, c, size);
}

static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) {
    dev_drawString(rx - dev_measureText(str, size), y, str, c, size);
}
#endif

static void drawWeatherIcon(int cx, int cy, int size, WeatherIcon iconType) {
    int r = size / 3;
    if (r < 3) r = 3;

    uint32_t cloudOutline = CC_BLACK;
    uint32_t cloudFill    = CC_WHITE;

    switch (iconType) {
        case WeatherIcon::Sun: { // Sun
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
        case WeatherIcon::Cloud: { // Cloud
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
        case WeatherIcon::Rain: { // Rain
            // cyShift and the drop x-offset must scale with size so the glyph stays
            // proportional when the icon is reflowed larger (see drawLandscapeWeatherColumn).
            int cyShift = cy - size / 6;   // == cy-4 at size 24
            dev_fillCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudFill);
            dev_drawCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudOutline);
            dev_fillCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudFill);
            dev_drawCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudOutline);
            dev_fillCircle(cx, cyShift - size / 10, size / 4, cloudFill);
            dev_drawCircle(cx, cyShift - size / 10, size / 4, cloudOutline);
            dev_fillRect(cx - size / 4, cyShift - size / 10, size / 2, size / 3, cloudFill);
            dev_drawFastHLine(cx - size / 3, cyShift + size / 4, (size * 2) / 3, cloudOutline);
            // Red rain drops (offsets proportional to size; == 6/3/9 at size 24)
            int dx = size / 4;
            int dropTop = cyShift + size / 4 + 3;
            int dropBot = cyShift + size / 2 + 3;
            dev_drawLine(cx - dx, dropTop, cx - dx - 3, dropBot, CC_RED);
            dev_drawLine(cx,      dropTop, cx - 3,       dropBot, CC_RED);
            dev_drawLine(cx + dx, dropTop, cx + dx - 3, dropBot, CC_RED);
            break;
        }
        case WeatherIcon::PartlyCloudy: // Partly cloudy
        default: {
            // Sun peeking behind cloud (stub rays scale with size so the glyph
            // stays proportional on the larger reflowed icon)
            int ray = size / 8;   // == 3 at size 24
            if (ray < 3) ray = 3;
            dev_fillCircle(cx - size / 4, cy - size / 5, size / 4, CC_YELLOW);
            dev_drawLine(cx - size / 4, cy - size / 5 - size / 4 - ray, cx - size / 4, cy - size / 5 - size / 4 - ray + 2, CC_YELLOW);
            dev_drawLine(cx - size / 4 - size / 4 - ray, cy - size / 5, cx - size / 4 - size / 4 - ray + 2, cy - size / 5, CC_YELLOW);
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
    int lines = 1, curX = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* wordStart = p;
        while (*p && *p != ' ') p++;
        int wordPx = cc_measurePxN(wordStart, (int)(p - wordStart), size);
        int spx = cc_advance(' ', size);
        if (curX > 0 && curX + wordPx > maxW) { lines++; curX = 0; }
        curX += wordPx + spx;
    }
    return lines;
}

// How many lines fit in maxH at this line height.
static int cc_lineCapacity(int maxH, int size, int lineHeight) {
    if (maxH < 8 * size) return 0;
    return (maxH - 8 * size) / lineHeight + 1;
}

// Width budget for one line. When the text will be cut off, the final line is kept
// short enough that the "..." marker still fits inline: 6 glyph widths of slack for
// centred text (the line floats, so both margins count).
static int cc_lineBudget(int maxW, int charWidth, bool truncated, int lineIdx, int capacity) {
    const int slack = 6;
    if (truncated && capacity > 0 && lineIdx == capacity - 1) {
        int budget = maxW - slack * charWidth;
        if (budget >= 4 * charWidth) return budget;
    }
    return maxW;
}

// Marks text the block could not hold. Prefers "..." right after the last word;
// when that does not fit, right-aligns ".." into the free gap at the block edge
// (without touching the word's ink); falls back to a single "." only when even
// that cannot be placed.
static void drawOverflowMarker(int x, int y, int maxRight, uint32_t color, int size) {
    int w = cc_advance('.', size);
    if (x + 3 * w <= maxRight) { dev_drawString(x, y, "...", color, size); return; }
    int two = maxRight - 2 * w;
    // Text's ink ends at x - w (the trailing space advance); start the dots there.
    if (two >= x - w) { dev_drawString(two, y, "..", color, size); return; }
    if (x + w <= maxRight) { dev_drawString(x, y, ".", color, size); return; }
    dev_drawString(maxRight - w, y, ".", color, size);
}

static int drawWrappedTextCentered(int centerX, int startY, int maxW, int maxH, const char* text, uint32_t color, int size = 1, int lineHeight = 10) {
    if (!text || !*text) return startY;

    const int charWidth = cc_advance(' ', size);      // reference advance for slack/budget
    const int lh = cc_lineHeight(size, lineHeight);
    const int capacity = cc_lineCapacity(maxH, size, lh);
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

        int budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);
        const char* p = lineStart;
        const char* lastWordEnd = lineStart;
        int linePx = 0;

        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            const char* wordStart = p;
            while (*p && *p != ' ') p++;
            int wordPx = cc_measurePxN(wordStart, (int)(p - wordStart), size);
            int spx = cc_advance(' ', size);
            int testPx = (linePx == 0) ? wordPx : (linePx + spx + wordPx);

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
        curY += lh;
        lineIdx++;
        lineStart = lastWordEnd;
    }

    if (truncated && lastLineWidth > 0) {
        drawOverflowMarker(centerX + lastLineWidth / 2 + charWidth, lastLineY, centerX + maxW / 2, color, size);
    }

    return curY;
}

static void drawVerseBlock(int startX, int startY, int maxW, int maxH, const VerseData& vd) {
    if (!vd.verse) return;

    // Auto-size the body font to the verse length: pick the largest of
    // 6 / 5.5 / 5pt that fits maxH lines in the box. Short verses get the
    // larger type; long ones fall back to 5pt. The main UI elsewhere stays at
    // the default 5.5pt via g_bodyFont.
#ifdef CHROMAWOTD_FONT_FREESANS
    const GFXfont* prevFont = g_bodyFont;
    const GFXfont* candidates[3] = { &Roboto6pt7b, &Roboto55pt7b, &Roboto5pt7b };
    for (int i = 0; i < 3; i++) {
        g_bodyFont = candidates[i];
        int lh = g_bodyFont->yAdvance;
        int cap = cc_lineCapacity(maxH, 1, lh);
        if (cc_wrappedLineCount(vd.verse, maxW, 1) <= cap) { break; }   // fits
    }
#endif

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
    int lineHeight = cc_lineHeight(1, 12);
    int charWidth = cc_advance(' ', 1);
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
        int wordPx = cc_measurePxN(wordStart, wordLen, 1);
        int wordIdx = (int)(wordStart - vd.verse);
        int budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);

        if (curX > startX && (curX + wordPx > startX + budget)) {
            curX = startX;
            curY += lineHeight;
            lineIdx++;
            if (curY + 8 > startY + maxH) break;
            budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);
        }

        // A word is highlighted when it overlaps the phrase range at all — not
        // merely when the phrase starts inside it.
        bool isHl = (hlStart >= 0 && wordIdx < hlEnd && wordIdx + wordLen > hlStart);
        uint32_t wordColor = isHl ? CC_RED : CC_BLACK;

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
        drawOverflowMarker(lastX, lastY, startX + maxW, CC_BLACK, 1);
    }

#if defined(CHROMAWOTD_FONT_FREESANS)
    cc_restoreBodyFont(prevFont);   // hand back the main-UI font
#endif
}

// 66 px-wide weather column. The alert block — rule, ALERT: label and wrapped
// text — is pinned to the BOTTOM when present (icon stays at the fixed 24px
// size). Without an alert the icon GROWS (up to 2x) and the temperature +
// condition push down, so the stack fills the column height instead of hugging
// the top and leaving a band of whitespace at the bottom.
static void drawLandscapeWeatherColumn(int cx, const WeatherData& w) {
    const int colW = 66;
    const int half = 28;

    // FORECAST header aligned with the yellow header box's bottom rule (y=13).
    int fcWidth = dev_measureText("FORECAST", 1);
    dev_drawString(cx - fcWidth / 2, 4, "FORECAST", CC_BLACK, 1);
    dev_drawFastHLine(cx - half, 13, 2 * half, CC_BLACK);   // aligns with header bottom (headerH-1)

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d°C", cc_roundTemp(w.temp));

    if (w.alert) {
        // --- Alert pinned to bottom; icon + temp at the fixed top layout. -----
        // Alert text is set in the 5pt body font (user preference); the rest of
        // the column stays at the default body size.
#ifdef CHROMAWOTD_FONT_FREESANS
        const GFXfont* prev = cc_setBodyFont(&Roboto5pt7b);
        const int alertLH = Roboto5pt7b.yAdvance;

        drawWeatherIcon(cx, 32, 24, w.icon);
        int tWidth = cc_measurePxF(&RobotoT10pt7b, tbuf, 1);
        dev_drawStringF(&RobotoT10pt7b, cx - tWidth / 2, 47, tbuf, CC_BLACK, 1);
#else
        const int alertLH = 10;
        drawWeatherIcon(cx, 32, 24, w.icon);
        int tWidth = dev_measureText(tbuf, 2);
        dev_drawString(cx - tWidth / 2, 47, tbuf, CC_BLACK, 2);
#endif

        int lines = cc_wrappedLineCount(w.alert, colW, 1);
        if (lines > 3) lines = 3;
        int textY  = 128 - 2 - 8 - (lines - 1) * alertLH;   // last line ends 2 px above bottom
        int labelY = textY - alertLH - 1;
        int divY   = labelY - 3;
        if (w.condition) {
            drawWrappedTextCentered(cx, 65, colW, divY - 2 - 65, w.condition, CC_BLACK, 1, cc_lineHeight(1, 10));
        }
        dev_drawFastHLine(cx - half, divY, 2 * half, CC_RED);
        int alWidth = dev_measureText("ALERT:", 1);
        dev_drawString(cx - alWidth / 2, labelY, "ALERT:", CC_RED, 1);
        drawWrappedTextCentered(cx, textY, colW, 128 - 2 - textY, w.alert, CC_RED, 1, alertLH);

#ifdef CHROMAWOTD_FONT_FREESANS
        cc_restoreBodyFont(prev);
#endif
        return;
    }

    // --- No alert: grow the icon to use the freed space, stack fills the column.
    const int colTop = 17;      // below the FORECAST rule
    const int colBot = 126;     // 2 px above the panel bottom
    const int availH = colBot - colTop;             // 109
    const int tempH  = 19;                          // size-2 temperature block (+1px gap to condition)
    const int gap    = 4;

    // Condition height depends on how many lines it wraps to (same estimate the
    // renderer uses), so the icon yields space as the condition grows.
    int condLines = w.condition ? cc_wrappedLineCount(w.condition, colW, 1) : 0;
    if (condLines > 5) condLines = 5;
    const int condH = condLines * 10 + 8;           // wrapped condition + 8px inset

    // Icon size = leftover column after temp + condition, clamped to [24, 48].
    int iconS = availH - tempH - condH - 2 * gap;
    if (iconS < 24) iconS = 24;
    else if (iconS > 48) iconS = 48;

    // Top-align under the header (2px pad). Any remaining whitespace collects
    // below the condition, so the column reads "filled".
    int topPad = 2;

    int iconCY = colTop + topPad + iconS / 2;
    drawWeatherIcon(cx, iconCY, iconS, w.icon);

    int tempY = colTop + topPad + iconS + gap;
#ifdef CHROMAWOTD_FONT_FREESANS
    int tWidth = cc_measurePxF(&RobotoT10pt7b, tbuf, 1);   // temp at native size (smooth)
    dev_drawStringF(&RobotoT10pt7b, cx - tWidth / 2, tempY, tbuf, CC_BLACK, 1);
#else
    int tWidth = dev_measureText(tbuf, 2);
    dev_drawString(cx - tWidth / 2, tempY, tbuf, CC_BLACK, 2);
#endif

    if (w.condition) {
        int condStart = tempY + tempH + gap;
        drawWrappedTextCentered(cx, condStart, colW, colBot - condStart, w.condition, CC_BLACK, 1, 10);
    }
}

void drawLayout(const VerseData& v, const WeatherData& w) {
    // Layout constants — single source of truth for geometry.
    // These replace the magic numbers scattered throughout drawLayout() and
    // drawLandscapeWeatherColumn(). Each constant has a one-line rationale.
    static constexpr int kPanelW = 296;
    static constexpr int kPanelH = 128;
    static constexpr int kSplitX = 226;       // divider x: verse ~10% wider than original 205
    static constexpr int kHeaderH = 14;       // was 20; smaller 5pt body needs less header height
    static constexpr int kWeatherCx = (kSplitX + 1 + kPanelW) / 2;  // center of weather column
    static constexpr int kVerseMargin = 4;    // verse block left margin
    static constexpr int kVerseMaxW = kSplitX - 2 * kVerseMargin;   // verse block width
    static constexpr int kVerseMaxH = 82;     // verse block height (fits 5 lines at 5.5pt)
    static constexpr int kReferenceY = 109;   // reference line y-position
    static constexpr int kWeatherColW = 66;   // weather column width (66px fits FORECAST + 3-line alert)
    static constexpr int kWeatherColHalf = 28; // half of kWeatherColW (for FORECAST rule)

    // 1. Header (Yellow band)
    dev_fillRect(0, 0, kSplitX, kHeaderH, CC_YELLOW);
    dev_drawString(6, 2, "Verse of the Day", CC_BLACK, 1);   // lifted 1px
    if (v.date) {
        dev_drawStringRight(kSplitX - 6, 2, v.date, CC_BLACK, 1);   // lifted 1px
    }
    dev_drawFastHLine(0, kHeaderH - 1, kSplitX, CC_BLACK);

    // 2. Verse body (White background) — box margins halved (8 -> 4)
    dev_fillRect(0, kHeaderH, kSplitX, kPanelH - kHeaderH, CC_WHITE);
    drawVerseBlock(kVerseMargin, kHeaderH + kVerseMargin, kVerseMaxW, kVerseMaxH, v);

    // 3. Reference line (Red) — moved 5px lower
    if (v.reference) {
        dev_drawFastHLine(10, kReferenceY, kSplitX - 20, CC_RED);
        dev_drawStringRight(kSplitX - 8, kReferenceY + 6, v.reference, CC_RED, 1);
    }

    // 4. Vertical divider
    dev_drawFastVLine(kSplitX, 0, kPanelH, CC_BLACK);

    // 5. Right-hand weather column (alert pinned to bottom)
    dev_fillRect(kSplitX + 1, 0, kPanelW - kSplitX - 1, kPanelH, CC_WHITE);
    drawLandscapeWeatherColumn(kWeatherCx, w);
}