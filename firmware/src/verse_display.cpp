#include "verse_display.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

// Shared UTF-8 -> single-byte decoder and wrapping math live in text/ (D1) so
// they are pure, unit-testable units rather than hidden in this monolith.
#include "net/portal.h"  // PortalInfo, for the setup screen at the end of this file
#include "net/wifi_qr.h" // cc_qrBuildWifiPayload, for the setup screen's QR code
#include "text/glyphs.h"
#include "text/wrap.h"
#include "third_party/qrcodegen/qrcodegen.h" // vendored QR encoder (MIT)

// ---------------------------------------------------------------------------
// Backend abstraction (D6). All drawing routes through the DisplayTarget
// interface so the layout code never sees Seeed GFX or the mock canvas directly.
// The two targets (CanvasTarget for host, SeeedTarget for device) own their own
// colour mapping and glyph rasterisation.
// ---------------------------------------------------------------------------
#include "draw/target.h"

static DisplayTarget& target() {
#ifdef CHROMAWOTD_HOST
    return getCanvasTarget();
#else
    return getSeeedTarget();
#endif
}

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
// GFXglyph/GFXfont/PROGMEM come from draw/font_types.h (host shim or device
// gfxfont.h via TFT_eSPI.h) — a single definition shared with the DisplayTarget
// backends, instead of a per-TU #ifdef shim.
#include "draw/font_types.h"
#include "fonts/Roboto55pt7b.h"  // body font (needs GFXglyph/GFXfont visible)
#include "fonts/Roboto5pt7b.h"   // auto-size small body (long verses)
#include "fonts/Roboto6pt7b.h"   // auto-size large body (short verses)
#include "fonts/RobotoT10pt7b.h" // temperature font (dedicated, native-size)
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
static void cc_restoreBodyFont(const GFXfont* prev) {
    g_bodyFont = prev;
}
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
            int a = -g->yOffset; // this glyph's ascent
            if (a > ab)
                ab = a;
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
    int r = cc_glyphAscentF(f, 1) / 4; // ~3 for the 10pt temp, ~2 for 5.5pt body
    if (r < 1)
        r = 1;
    if (r > 3)
        r = 3;
    return r;
}

// Draw a degree circle for a given font: a small superscript circle whose top
// aligns near the font's cap top and whose left sits at the cursor+radius.
// Shared by host + device — both route the circle through the target.
static void dev_drawDegree(const GFXfont* f, int cursorX, int base, uint32_t c, int size) {
    int r = cc_degreeRadius(f) * size;
    int cx = cursorX + r;
    int cy = base - cc_glyphAscentF(f, size) + r; // top near cap top
    target().drawCircle(cx, cy, r, c);
}
#endif // CHROMAWOTD_FONT_FREESANS

// Pixel width of a decoded string at the given magnification.
static int cc_measurePx(const char* str, int size) {
    if (!str)
        return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char g = 0;
        p += cc_utf8ToAscii(p, &g);
        w += cc_advance(g, size);
    }
    return w;
}

// Pixel width of a decoded string in a specific GFX font (temperature etc.).
#ifdef CHROMAWOTD_FONT_FREESANS
static int cc_measurePxF(const GFXfont* f, const char* str, int size) {
    if (!str)
        return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char g = 0;
        p += cc_utf8ToAscii(p, &g);
        w += cc_advanceF(f, g, size);
    }
    return w;
}
#endif // CHROMAWOTD_FONT_FREESANS

// Same, but for a substring of `len` bytes (word-splitting callers). Uses the
// length-bounded decoder so a multi-byte sequence straddling `len` is not read
// past the declared bound (see S2).
static int cc_measurePxN(const char* str, int len, int size) {
    if (!str || len <= 0)
        return 0;
    int w = 0;
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    while (p < end) {
        unsigned char g = 0;
        p += cc_utf8ToAsciiN(p, end, &g);
        w += cc_advance(g, size);
    }
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

// Round half away from zero: -0.6 -> -1 (plain (int)(t+0.5f) gives 0).
static int cc_roundTemp(float t) {
    return (int)lroundf(t);
}

// Case-insensitive substring search (fallback for highlight phrases whose
// capitalisation is changed by the content source).
static const char* cc_findIgnoreCase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle)
        return nullptr;
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return p;
    }
    return nullptr;
}

bool verseHighlightFound(const VerseData& vd) {
    if (!vd.verse || !vd.highlight || !vd.highlight[0])
        return false;
    return cc_findIgnoreCase(vd.verse, vd.highlight) != nullptr;
}

// ---------------------------------------------------------------------------
// Unified backend. The dev_* primitives forward to the DisplayTarget, and the
// FreeSans glyph/degree special-casing lives in the shared code below so host and
// device agree byte-for-byte. There is exactly one copy of each, not two under
// #ifdef CHROMAWOTD_HOST.
// ---------------------------------------------------------------------------

static void dev_fillRect(int x, int y, int w, int h, uint32_t c) {
    target().fillRect(x, y, w, h, c);
}
static void dev_drawRect(int x, int y, int w, int h, uint32_t c) {
    target().drawRect(x, y, w, h, c);
}
static void dev_drawFastHLine(int x, int y, int w, uint32_t c) {
    target().drawFastHLine(x, y, w, c);
}
static void dev_drawFastVLine(int x, int y, int h, uint32_t c) {
    target().drawFastVLine(x, y, h, c);
}
static void dev_drawLine(int x0, int y0, int x1, int y1, uint32_t c) {
    target().drawLine(x0, y0, x1, y1, c);
}
static void dev_drawCircle(int x, int y, int r, uint32_t c) {
    target().drawCircle(x, y, r, c);
}
static void dev_fillCircle(int x, int y, int r, uint32_t c) {
    target().fillCircle(x, y, r, c);
}

static int dev_measureText(const char* str, int size) {
    return cc_measurePx(str, size);
}

#ifdef CHROMAWOTD_FONT_FREESANS
// FreeSans/Roboto path: per-glyph decode, baseline = y + glyphAscent, degree drawn
// as a vector circle, glyphs drawn at baseline+yOffset.
static void dev_drawStringF(const GFXfont* f, int x, int y, const char* str, uint32_t c, int size) {
    if (!str)
        return;
    int cursorX = x;
    int base = y + cc_glyphAscentF(f, size); // replicate TFT_eSPI poY += glyph_ab
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        if (glyph == CC_DEGREE) {
            dev_drawDegree(f, cursorX, base, c, size);
        } else {
            target().drawGlyphF(f, glyph, cursorX, base, c, size);
        }
        cursorX += cc_advanceF(f, glyph, size);
    }
}

static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    dev_drawStringF(g_bodyFont, x, y, str, c, size);
}
#else
// Built-in 5x7 path: fixed 6px advance, degree drawn as a small circle at
// (cursorX + 2*size, y + 2*size, r=size).
static void dev_drawString(int x, int y, const char* str, uint32_t c, int size) {
    if (!str)
        return;
    int cursorX = x;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        unsigned char glyph = 0;
        p += cc_utf8ToAscii(p, &glyph);
        if (glyph == CC_DEGREE) {
            target().drawCircle(cursorX + 2 * size, y + 2 * size, size, c);
        } else {
            target().drawChar(cursorX, y, glyph, c, (uint8_t)size);
        }
        cursorX += CC_GLYPH_W * size;
    }
}
#endif

static void dev_drawStringRight(int rx, int y, const char* str, uint32_t c, int size) {
    dev_drawString(rx - dev_measureText(str, size), y, str, c, size);
}

// Lines the greedy wrapper below needs for a given width budget (mirrors the
// draw loops exactly, so the truncation decision is made before drawing).
static int cc_wrappedLineCount(const char* text, int maxW, int size) {
    if (!text || !*text)
        return 0;
    int lines = 1, curX = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char* wordStart = p;
        while (*p && *p != ' ')
            p++;
        int wordPx = cc_measurePxN(wordStart, (int)(p - wordStart), size);
        int spx = cc_advance(' ', size);
        if (curX > 0 && curX + wordPx > maxW) {
            lines++;
            curX = 0;
        }
        curX += wordPx + spx;
    }
    return lines;
}

// How many lines fit in maxH at this line height.
// (cc_lineCapacity and cc_lineBudget moved to text/wrap.cpp — pure math.)

// Auto-size the verse body font: the largest of Roboto 6 / 5.5 / 5pt whose
// wrapped line count still fits the block height. Extracted from
// drawVerseBlock() so the host suite can assert the selection directly
// instead of pixel-probing the render (LESSONS §38).
#ifdef CHROMAWOTD_FONT_FREESANS
#include "fonts/Roboto10pt7b.h"
#include "fonts/Roboto7pt7b.h"
#include "fonts/Roboto8pt7b.h"
#include "fonts/Roboto9pt7b.h"
static const GFXfont* cc_pickVerseFont(const char* verse, int maxW, int maxH) {
    static const GFXfont* const candidates[7] = {&Roboto10pt7b, &Roboto9pt7b,  &Roboto8pt7b, &Roboto7pt7b,
                                                 &Roboto6pt7b,  &Roboto55pt7b, &Roboto5pt7b};
    // The wrap measurement routes through cc_advance(), which reads the global
    // g_bodyFont — so the candidate must BE the active body font while its line
    // count is measured (measuring every candidate at one font collapses the
    // ladder; see LESSONS §38).
    const GFXfont* prev = g_bodyFont;
    const GFXfont* pick = &Roboto5pt7b; // nothing fit: smallest font
    for (int i = 0; i < 7; i++) {
        g_bodyFont = candidates[i];
        const int cap = cc_lineCapacity(maxH, 1, candidates[i]->yAdvance);
        if (cc_wrappedLineCount(verse, maxW, 1) <= cap) {
            pick = candidates[i];
            break;
        }
    }
    g_bodyFont = prev;
    return pick;
}

VerseFontSize cc_verseFontSize(const char* verse, int maxW, int maxH) {
    const GFXfont* f = cc_pickVerseFont(verse, maxW, maxH);
    if (f == &Roboto10pt7b)
        return VerseFontSize::Pt10;
    if (f == &Roboto9pt7b)
        return VerseFontSize::Pt9;
    if (f == &Roboto8pt7b)
        return VerseFontSize::Pt8;
    if (f == &Roboto7pt7b)
        return VerseFontSize::Pt7;
    if (f == &Roboto6pt7b)
        return VerseFontSize::Pt6;
    if (f == &Roboto5pt7b)
        return VerseFontSize::Pt5;
    return VerseFontSize::Pt55;
}
#else
VerseFontSize cc_verseFontSize(const char* verse, int maxW, int maxH) {
    (void)verse;
    (void)maxW;
    (void)maxH;
    return VerseFontSize::Pt55; // auto-size compiled out: fixed default body font
}
#endif

// Marks text the block could not hold. Prefers "..." right after the last word;
// when that does not fit, right-aligns ".." into the free gap at the block edge
// (without touching the word's ink); falls back to a single "." only when even
// that cannot be placed.
static void drawOverflowMarker(int x, int y, int maxRight, uint32_t color, int size) {
    int w = cc_advance('.', size);
    if (x + 3 * w <= maxRight) {
        dev_drawString(x, y, "...", color, size);
        return;
    }
    int two = maxRight - 2 * w;
    // Text's ink ends at x - w (the trailing space advance); start the dots there.
    if (two >= x - w) {
        dev_drawString(two, y, "..", color, size);
        return;
    }
    if (x + w <= maxRight) {
        dev_drawString(x, y, ".", color, size);
        return;
    }
    dev_drawString(maxRight - w, y, ".", color, size);
}

static int drawWrappedTextCentered(int centerX, int startY, int maxW, int maxH, const char* text, uint32_t color,
                                   int size = 1, int lineHeight = 10) {
    if (!text || !*text)
        return startY;

    const int charWidth = cc_advance(' ', size); // reference advance for slack/budget
    const int lh = cc_lineHeight(size, lineHeight);
    const int capacity = cc_lineCapacity(maxH, size, lh);
    const bool truncated = cc_wrappedLineCount(text, maxW, size) > capacity;

    int curY = startY;
    int lineIdx = 0;
    int lastLineWidth = 0;
    int lastLineY = startY;

    const char* lineStart = text;
    while (*lineStart) {
        while (*lineStart == ' ')
            lineStart++;
        if (!*lineStart)
            break;

        if (curY + 8 * size > startY + maxH)
            break;

        int budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);
        const char* p = lineStart;
        const char* lastWordEnd = lineStart;
        int linePx = 0;

        while (*p) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;

            const char* wordStart = p;
            while (*p && *p != ' ')
                p++;
            int wordPx = cc_measurePxN(wordStart, (int)(p - wordStart), size);
            int spx = cc_advance(' ', size);
            int testPx = (linePx == 0) ? wordPx : (linePx + spx + wordPx);

            if (testPx > budget && linePx > 0) {
                break;
            }
            linePx = testPx;
            lastWordEnd = p;
        }

        // Render this line centered.
        // lineBuf is the boundary for remote text: it holds at most 95 bytes of a
        // wrapped line, copied with a bounded memcpy (never sprintf) so a hostile or
        // over-long remote string cannot overflow. Truncation at 95 bytes cuts the
        // line mid-word with no marker (see C1); lines are <= ~14 glyphs in practice.
        char lineBuf[96];
        int bytes = (int)(lastWordEnd - lineStart);
        if (bytes > 95)
            bytes = 95;
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
    if (!vd.verse)
        return;

    // Auto-size the body font to the verse length: pick the largest of
    // 6 / 5.5 / 5pt that fits maxH lines in the box. Short verses get the
    // larger type; long ones fall back to 5pt. The main UI elsewhere stays at
    // the default 5.5pt via g_bodyFont.
#ifdef CHROMAWOTD_FONT_FREESANS
    const GFXfont* prevFont = g_bodyFont;
    g_bodyFont = cc_pickVerseFont(vd.verse, maxW, maxH);
#endif

    // Locate the highlighted phrase: exact match first, then case-insensitive so a
    // capitalisation change in the source does not silently drop the red accent.
    int hlStart = -1, hlEnd = -1;
    if (vd.highlight && vd.highlight[0]) {
        const char* p = strstr(vd.verse, vd.highlight);
        if (!p)
            p = cc_findIgnoreCase(vd.verse, vd.highlight);
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
        while (*ptr == ' ')
            ptr++;
        if (!*ptr)
            break;

        if (curY + 8 > startY + maxH)
            break;

        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ')
            ptr++;
        int wordLen = (int)(ptr - wordStart);
        int wordPx = cc_measurePxN(wordStart, wordLen, 1);
        int wordIdx = (int)(wordStart - vd.verse);
        int budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);

        if (curX > startX && (curX + wordPx > startX + budget)) {
            curX = startX;
            curY += lineHeight;
            lineIdx++;
            if (curY + 8 > startY + maxH)
                break;
            budget = cc_lineBudget(maxW, charWidth, truncated, lineIdx, capacity);
        }

        // A word is highlighted when it overlaps the phrase range at all — not
        // merely when the phrase starts inside it.
        bool isHl = (hlStart >= 0 && wordIdx < hlEnd && wordIdx + wordLen > hlStart);
        uint32_t wordColor = isHl ? CC_RED : CC_BLACK;

        // wordBuf is the other remote-text boundary: a single word copied with a
        // bounded memcpy into 63 bytes (never sprintf). Words longer than 63 bytes
        // are silently truncated; verses use ~14-glyph words at most, so this is
        // latent — the cap is deliberate and never removed without a marker plan.
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
    cc_restoreBodyFont(prevFont); // hand back the main-UI font
#endif
}

// Fit `src` into `dst`, truncating with a VISIBLE "..." when it cannot fit maxW.
// The verse block already follows this rule (never drop words silently); the footer row
// needs it too, because an over-long warning was previously drawn unbounded and ran off
// the panel edge.
static void cc_fitText(const char* src, char* dst, size_t dstsz, int maxW) {
    if (!dst || dstsz == 0)
        return;
    snprintf(dst, dstsz, "%s", src ? src : "");
    const int ellipsisW = dev_measureText("...", 1);
    if (maxW < ellipsisW) { // no room to say anything honestly
        dst[0] = '\0';
        return;
    }
    if (dev_measureText(dst, 1) <= maxW)
        return;
    size_t n = strlen(dst);
    while (n > 0 && dev_measureText(dst, 1) > maxW - ellipsisW)
        dst[--n] = '\0';
    snprintf(dst + n, dstsz - n, "...");
}

// Width of a header-band string (the identity line or the date). BOTH are drawn one size
// up (7pt) on the device font path, so the collision guard must measure with that same
// font — measuring with the 5.5pt body default would under-report and let a long word
// overprint the date.
static int cc_headWidth(const char* s) {
#ifdef CHROMAWOTD_FONT_FREESANS
    return cc_measurePxF(&Roboto7pt7b, s, 1);
#else
    return dev_measureText(s, 1);
#endif
}

void drawLayout(const VerseData& v, const WeatherData& w, const LayoutOptions& opts) {
    // Layout constants — single source of truth for geometry. Each has a one-line rationale.
    // The verse owns the WHOLE panel: there is no weather column and no divider.
    static constexpr int kPanelW = 296;
    static constexpr int kPanelH = 128;
    static constexpr int kHeaderH = 18;    // tall enough for the 7pt identity line
    static constexpr int kVerseMargin = 4; // text block left/right margin
    static constexpr int kHeaderTextX = 6; // identity line x
    static constexpr int kRowY = 115;      // footer row (status + weather)

    const bool haveAlert = (w.alert && w.alert[0]);

    // --- 1. Header band: WHAT you are reading -------------------------------------
    // The word with its respelling, or the verse's citation. The mode name ("Verse of the
    // Day") was the least informative text on the panel; this is the line you glance at.
    char head[96];
    if (opts.leftCaption && opts.leftCaption[0] && v.reference)
        snprintf(head, sizeof(head), "%s %s", v.reference, opts.leftCaption);
    else if (v.reference)
        snprintf(head, sizeof(head), "%s", v.reference);
    else
        snprintf(head, sizeof(head), "%s", opts.headerTitle ? opts.headerTitle : "");

    // The identity line must CLEAR the date, so degrade deliberately rather than let it
    // overprint: drop the respelling first, then truncate. BOTH margins come out of the
    // budget, so reserving 8px of clear space needs -12, not -6.
    const int dateW = v.date ? cc_headWidth(v.date) : 0; // measured at the size it is DRAWN
    const int headRoom = kPanelW - kHeaderTextX - 6 - dateW - 8;
    if (v.reference && opts.leftCaption && opts.leftCaption[0] && cc_headWidth(head) > headRoom)
        snprintf(head, sizeof(head), "%s", v.reference); // respelling dropped
    for (size_t n = strlen(head); n > 1 && cc_headWidth(head) > headRoom; n--)
        head[n - 1] = '\0';

    dev_fillRect(0, 0, kPanelW, kHeaderH, CC_YELLOW);
#ifdef CHROMAWOTD_FONT_FREESANS
    // One size up from the 5.5pt body default: this is the line you scan first.
    dev_drawStringF(&Roboto7pt7b, kHeaderTextX, 1, head, CC_BLACK, 1);
#else
    dev_drawString(kHeaderTextX, 2, head, CC_BLACK, 1);
#endif
    if (v.date) {
#ifdef CHROMAWOTD_FONT_FREESANS
        // Same size as the identity line: the band reads as a single line rather than a
        // large left item beside a small right one.
        dev_drawStringF(&Roboto7pt7b, kPanelW - 6 - cc_headWidth(v.date), 1, v.date, CC_BLACK, 1);
#else
        dev_drawStringRight(kPanelW - 6, 2, v.date, CC_BLACK, 1);
#endif
    }
    dev_drawFastHLine(0, kHeaderH - 1, kPanelW, CC_BLACK);

    // --- 2. Body: the verse at the whole panel width, auto-sized ------------------
    dev_fillRect(0, kHeaderH, kPanelW, kPanelH - kHeaderH, CC_WHITE);
    drawVerseBlock(kVerseMargin, kHeaderH + kVerseMargin, kVerseMaxW, kVerseMaxH, v);

    // --- 3. Footer row: status/warnings LEFT, weather RIGHT -----------------------
    // Weather is computed first because its width bounds the warning. It is TEXT, not an
    // icon: the condition words carry more than a 10px glyph, and the 4-colour icon only
    // ever distinguished four states.
    const int rowRoom = kPanelW - 2 * kVerseMargin;
    char wtbuf[16] = "";
    char wcbuf[48] = "";
    const char* tag = nullptr;
    int weatherW = 0;
    if (w.valid) {
        snprintf(wtbuf, sizeof(wtbuf), "%d°C", cc_roundTemp(w.temp));
        // "TOMORROW" only when it matters: in the evening the number is TOMORROW's high and
        // would otherwise be indistinguishable from today's.
        tag = (opts.weatherLabel && strcmp(opts.weatherLabel, "FORECAST") != 0) ? opts.weatherLabel : nullptr;
        snprintf(wcbuf, sizeof(wcbuf), "%s", (w.condition && w.condition[0]) ? w.condition : "");
        const int sep = 4;
        const int tW = dev_measureText(wtbuf, 1);
        weatherW = tW;
        if (wcbuf[0])
            weatherW += sep + dev_measureText(wcbuf, 1);
        if (tag)
            weatherW += sep + dev_measureText(tag, 1);
        // Degrade to fit the row on its own: drop the condition, then the label. The
        // temperature always survives — it is the one value worth the space.
        if (weatherW > rowRoom && wcbuf[0]) {
            wcbuf[0] = '\0';
            weatherW = tW + (tag ? sep + dev_measureText(tag, 1) : 0);
        }
        if (weatherW > rowRoom && tag) {
            tag = nullptr;
            weatherW = tW;
        }
    }

    if (haveAlert) {
        // A weather warning or a fetch failure. Truncated VISIBLY if it cannot fit beside
        // the weather — an unbounded draw here ran off the panel edge.
        char abuf[96];
        cc_fitText(w.alert, abuf, sizeof(abuf), rowRoom - weatherW - (weatherW ? 8 : 0));
        if (abuf[0])
            dev_drawString(kVerseMargin, kRowY, abuf, CC_RED, 1);
    }

    if (weatherW > 0) {
        const int sep = 4;
        int x = kPanelW - kVerseMargin - weatherW;
        if (tag) {
            dev_drawString(x, kRowY, tag, CC_RED, 1); // red: it changes what the number means
            x += dev_measureText(tag, 1) + sep;
        }
        dev_drawString(x, kRowY, wtbuf, CC_BLACK, 1);
        if (wcbuf[0])
            dev_drawString(x + dev_measureText(wtbuf, 1) + sep, kRowY, wcbuf, CC_BLACK, 1);
    }
}

// Draw a QR code for `text`, anchored at (x, y), within `maxH` pixels of height.
// Returns the x coordinate just past the drawn code (so a caller can lay text out
// beside it), or `x` when nothing could be drawn.
//
// Why the buffers are static: qrcodegen_encodeText needs two caller buffers of
// qrcodegen_BUFFER_LEN_MAX (3918 bytes each at the largest QR version). On a device
// with a 16 KB sync-task stack that is fatal on the stack, so they live in BSS — the
// draw happens once per boot and never concurrently.
//
// Scale selection is what makes this scannable: modules are drawn as `scale`x`scale`
// pixel blocks and there is NO partial scaling, so the code stays on the module grid.
// The largest scale that fits both the height budget and the panel width wins.
static int drawPortalQrCode(const char* text, int x, int y, int maxH) {
    static uint8_t qrTemp[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qrCode[qrcodegen_BUFFER_LEN_MAX];

    const bool ok = qrcodegen_encodeText(text, qrTemp, qrCode, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                                         qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    if (!ok)
        return x; // payload too long: no code rather than a wrong code

    const int size = qrcodegen_getSize(qrCode); // modules per side (no quiet zone)
    // The spec requires a 4-module quiet zone; without it scanners fail on a busy
    // background. It is drawn as white margin around the modules.
    const int quiet = 4;
    const int totalModules = size + 2 * quiet;

    // Largest whole-module scale that fits the height budget and stays on-panel.
    int scale = maxH / totalModules;
    const int maxScaleByWidth = (296 - x - 2) / totalModules;
    if (scale > maxScaleByWidth)
        scale = maxScaleByWidth;
    if (scale < 1)
        return x; // too dense to render legibly: caller shows text only

    const int px = totalModules * scale;
    const int ox = x;                   // quiet zone is inside `px`
    const int oy = y + (maxH - px) / 2; // vertically centred in the budget

    // Quiet zone: a white block behind the code (the panel is white already, but this
    // guarantees the margin even over an earlier drawing).
    dev_fillRect(ox, oy, px, px, CC_WHITE);

    for (int my = 0; my < size; my++) {
        for (int mx = 0; mx < size; mx++) {
            if (!qrcodegen_getModule(qrCode, mx, my))
                continue;
            const int px0 = ox + (mx + quiet) * scale;
            const int py0 = oy + (my + quiet) * scale;
            dev_fillRect(px0, py0, scale, scale, CC_BLACK);
        }
    }
    return ox + px;
}

// Setup screen for the first-boot captive portal (see net/portal.cpp). Lives here
// because the dev_drawString* helpers above are file-static and this must reuse
// exactly the same text/degree/decoding path as every other screen — a second copy
// in portal.cpp would be the thing that drifts.
//
// Layout: a Wi-Fi QR code on the LEFT (the primary path — the user scans and the
// phone joins the AP with no typing), and the same credentials in text on the RIGHT
// as the fallback, with the setup URL. The QR is square and needs a quiet zone, so
// most of the right side is text at size 1.
void cc_portalDrawScreen(const PortalInfo& info, PortalNotice notice, const char* statusLine) {
    static constexpr int kW = 296;
    static constexpr int kH = 128;
    static constexpr int kHeaderH = 16;

    dev_fillRect(0, 0, kW, kH, CC_WHITE);
    dev_fillRect(0, 0, kW, kHeaderH, CC_YELLOW);
    dev_drawString(6, 4, "ChromaWOTD setup", CC_BLACK, 1);
    dev_drawFastHLine(0, kHeaderH - 1, kW, CC_BLACK);

    if (notice != PortalNotice::None && statusLine && statusLine[0]) {
        // Show the reason in red rather than the setup steps, so it is unambiguous why the
        // panel came back here. The HEADING depends on what actually happened: a rejected
        // save and a report that stored settings were lost are different situations, and
        // labelling the second one "Could not save:" would be plainly wrong.
        const char* heading = (notice == PortalNotice::SettingsLost) ? "Settings lost:" : "Could not save:";
        dev_drawString(6, 24, heading, CC_RED, 1);
        drawWrappedTextCentered(kW / 2, 36, kW - 12, 84, statusLine, CC_RED, 1, 10);
        return;
    }

    // --- QR code (left) -----------------------------------------------------
    // Drawn first so the text block can be positioned against its actual extent.
    // The QR gets almost the full panel height (not a reduced band) because module
    // size is the difference between scanning and not: at ECC LOW a 51-char payload
    // is version 3 (29 modules), so 37 modules with the quiet zone scale to 3px
    // (111px) when given ~111px of height, versus only 2px (74px) from a 108px band.
    char payload[CC_QR_PAYLOAD_MAX];
    const size_t plen = cc_qrBuildWifiPayload(payload, sizeof(payload), info.apName, info.apPassword, "WPA");
    int qrRight = 4;
    if (plen > 0) {
        qrRight = drawPortalQrCode(payload, 4, kHeaderH + 1, kH - kHeaderH - 2);
    }

    // --- Credentials as text (right) ----------------------------------------
    // The fallback for a phone whose scanner will not cooperate, or whose OS blocks the
    // portal probe via Private DNS / an always-on VPN (in which case no sign-in sheet
    // appears at all and this text is the only way in).
    //
    // Every line is flush-left at `tx` for a consistent left edge. These strings are
    // known-width and all fit `tw` (the widest is 24 chars = 144px of the 202px
    // available), and apName is always exactly 17 chars ("ChromaWOTD-" + 6 hex), so
    // nothing here can wrap — an earlier version centred two of the lines and it read
    // as a misalignment rather than a deliberate choice.
    const int tx = qrRight + 6;
    const int tw = kW - tx - 4; // keep a 4px right margin
    int y = kHeaderH + 3;
    dev_drawString(tx, y, "Scan the code, or type:", CC_BLACK, 1);
    y += 12;

    dev_drawString(tx, y, info.apName, CC_RED, 1);
    y += 12;

    dev_drawString(tx, y, "Password:", CC_BLACK, 1);
    y += 11;
    // The password uses the DEDICATED 10pt font at its NATIVE size rather than scaling
    // the 5.5pt body font up 2x. Scaling a small bitmap stair-steps the digits, and an
    // 8-digit code is exactly the thing that has to be unambiguous when typed off a
    // 4-colour panel; the 10pt glyphs are drawn unscaled so the curves stay smooth (the
    // temperature readout uses this same font for the same reason).
    //
    // Grouped in pairs ("2668 1037") because an unbroken 8-digit run is hard to parse
    // at a glance, and a mis-typed AP password means the user cannot reach the form.
    char pwGrouped[CC_PORTAL_APASS_MAX + 4];
    size_t gi = 0;
    for (size_t i = 0; info.apPassword[i] && gi + 2 < sizeof(pwGrouped); i++) {
        if (i > 0 && i % 4 == 0)
            pwGrouped[gi++] = ' ';
        pwGrouped[gi++] = info.apPassword[i];
    }
    pwGrouped[gi] = '\0';
#ifdef CHROMAWOTD_FONT_FREESANS
    dev_drawStringF(&RobotoT10pt7b, tx, y, pwGrouped, CC_BLACK, 1);
#else
    dev_drawString(tx, y, pwGrouped, CC_BLACK, 2);
#endif
    y += 22;

    dev_drawString(tx, y, "Open http://192.168.4.1", CC_BLACK, 1);
    y += 11;
    dev_drawString(tx, y, "Reset: hold a button 10s", CC_BLACK, 1);
}
