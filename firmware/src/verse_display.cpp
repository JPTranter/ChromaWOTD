#include "verse_display.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef CHROMAWOTD_HOST
#include "../test/harness/canvas.h"

static void dev_fillRect(int x, int y, int w, int h, uint32_t c) { g_canvas.fillRect(x, y, w, h, c); }
static void dev_drawRect(int x, int y, int w, int h, uint32_t c) { g_canvas.drawRect(x, y, w, h, c); }
static void dev_drawFastHLine(int x, int y, int w, uint32_t c) { g_canvas.drawFastHLine(x, y, w, c); }
static void dev_drawFastVLine(int x, int y, int h, uint32_t c) { g_canvas.drawFastVLine(x, y, h, c); }
static void dev_drawLine(int x0, int y0, int x1, int y1, uint32_t c) { g_canvas.drawLine(x0, y0, x1, y1, c); }
static void dev_drawCircle(int x, int y, int r, uint32_t c) { g_canvas.drawCircle(x, y, r, c); }
static void dev_fillCircle(int x, int y, int r, uint32_t c) { g_canvas.fillCircle(x, y, r, c); }
static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) { g_canvas.drawString(x, y, str, c, size); }
static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) { g_canvas.drawStringRight(rx, y, str, c, size); }
static int  dev_measureText(const char* str, int size) { return g_canvas.measureText(str, size); }

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
static int dev_measureText(const char* str, int size) {
    if (!str) return 0;
    int count = 0;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        if (*p == 0xC2 && *(p + 1) == 0xB0) { count++; p++; continue; }
        if (*p == 0xE2 && *(p + 1) == 0x80 && (*(p + 2) == 0x93 || *(p + 2) == 0x94)) { count++; p += 2; continue; }
        count++;
    }
    return count * 6 * size;
}

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    if (!str) return;
    epaper.setTextSize(size);
    epaper.setTextColor(toDeviceColor(c));
    int cursorX = x;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        // Handle utf-8 degree symbol: 0xC2 0xB0
        if (*p == 0xC2 && *(p + 1) == 0xB0) {
            int r = size;
            epaper.drawCircle(cursorX + 2 * size, y + 2 * size, r, toDeviceColor(c));
            cursorX += 6 * size;
            p++;
            continue;
        }
        // Handle utf-8 en-dash: 0xE2 0x80 0x93 or em-dash: 0x94
        if (*p == 0xE2 && *(p + 1) == 0x80 && (*(p + 2) == 0x93 || *(p + 2) == 0x94)) {
            epaper.drawChar('-', cursorX, y);
            cursorX += 6 * size;
            p += 2;
            continue;
        }
        epaper.drawChar(*p, cursorX, y);
        cursorX += 6 * size;
    }
}

static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) {
    int w = dev_measureText(str, size);
    dev_drawString(rx - w, y, str, c, size);
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

static int drawWrappedText(int startX, int startY, int maxW, int maxH, const char* text, uint32_t color, int size = 1, int lineHeight = 10) {
    if (!text || !*text) return startY;

    int charWidth = 6 * size;
    int curX = startX;
    int curY = startY;

    const char* ptr = text;
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (!*ptr) break;

        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ') ptr++;
        int wordLen = (int)(ptr - wordStart);
        int wordPx = wordLen * charWidth;

        if (curX > startX && (curX + wordPx > startX + maxW)) {
            curX = startX;
            curY += lineHeight;
            if (curY + 8 * size > startY + maxH) break;
        }

        char wordBuf[64];
        int copyLen = wordLen < 63 ? wordLen : 63;
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        dev_drawString(curX, curY, wordBuf, color, size);
        curX += wordPx + charWidth;
    }
    return curY + lineHeight;
}

static int drawWrappedTextCentered(int centerX, int startY, int maxW, int maxH, const char* text, uint32_t color, int size = 1, int lineHeight = 10) {
    if (!text || !*text) return startY;

    int charWidth = 6 * size;
    int curY = startY;

    const char* lineStart = text;
    while (*lineStart) {
        while (*lineStart == ' ') lineStart++;
        if (!*lineStart) break;

        const char* p = lineStart;
        const char* lastWordEnd = lineStart;
        int lineLen = 0;

        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            const char* wordStart = p;
            while (*p && *p != ' ') p++;
            int wordLen = (int)(p - wordStart);
            int testLen = (lineLen == 0) ? wordLen : (lineLen + 1 + wordLen);

            if (testLen * charWidth > maxW && lineLen > 0) {
                break;
            }
            lineLen = testLen;
            lastWordEnd = p;
        }

        if (curY + 8 * size > startY + maxH) break;

        // Render this line centered
        char lineBuf[96];
        int bytes = (int)(lastWordEnd - lineStart);
        if (bytes > 95) bytes = 95;
        memcpy(lineBuf, lineStart, bytes);
        lineBuf[bytes] = '\0';

        int lineWidth = dev_measureText(lineBuf, size);
        dev_drawString(centerX - lineWidth / 2, curY, lineBuf, color, size);

        curY += lineHeight;
        lineStart = lastWordEnd;
    }
    return curY;
}



static void drawVerseBlock(int startX, int startY, int maxW, int maxH, const VerseData& vd, bool inverted = false) {
    if (!vd.verse) return;

    int hlStart = -1, hlEnd = -1;
    if (vd.highlight && vd.highlight[0]) {
        const char* p = strstr(vd.verse, vd.highlight);
        if (p) {
            hlStart = (int)(p - vd.verse);
            hlEnd = hlStart + (int)strlen(vd.highlight);
        }
    }

    int curX = startX;
    int curY = startY;
    int lineHeight = 12;
    int charWidth = 6;

    const char* ptr = vd.verse;
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (!*ptr) break;

        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ') ptr++;
        int wordLen = (int)(ptr - wordStart);
        int wordPx = wordLen * charWidth;
        int wordIdx = (int)(wordStart - vd.verse);

        if (curX > startX && (curX + wordPx > startX + maxW)) {
            curX = startX;
            curY += lineHeight;
            if (curY + 8 > startY + maxH) break;
        }

        bool isHl = (hlStart >= 0 && wordIdx >= hlStart && wordIdx < hlEnd);
        uint32_t wordColor = isHl ? (inverted ? CC_YELLOW : CC_RED) : (inverted ? CC_WHITE : CC_BLACK);

        char wordBuf[64];
        int copyLen = wordLen < 63 ? wordLen : 63;
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        dev_drawString(curX, curY, wordBuf, wordColor, 1);
        curX += wordPx + charWidth;
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
    snprintf(tbuf, sizeof(tbuf), "%d°C", (int)(w.temp + 0.5f));
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
    snprintf(tbuf, sizeof(tbuf), "%d°C", (int)(w.temp + 0.5f));
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
    snprintf(tbuf, sizeof(tbuf), "%d°C", (int)(w.temp + 0.5f));
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
    snprintf(tbuf, sizeof(tbuf), "%d°C", (int)(w.temp + 0.5f));
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
    snprintf(tbuf, sizeof(tbuf), "%d°C", (int)(w.temp + 0.5f));
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


