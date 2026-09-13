// test_layout_overflow.cpp — invariants that a pixel-probe suite cannot catch:
// overflow spill, truncation markers, region containment, temperature rounding,
// highlight robustness and UTF-8 normalisation. All targets the single light,
// landscape layout.
#include "../harness/canvas.h"
#include "verse_display.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

// ---------------------------------------------------------------- helpers ---

static std::string canvasHash() {
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (uint8_t b : g_canvas.px) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return std::to_string(h);
}

static bool rectAllColor(int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) != color)
                return false;
    return true;
}

static int colorCount(int x0, int y0, int x1, int y1, uint32_t color) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) == color)
                n++;
    return n;
}

// The ellipsis overflow marker is a run of '.' glyphs, and its ink shape is
// FONT-SPECIFIC, so the detector is per font family (LESSONS 39):
//   - 5x7 (fallback font): '.' is a 2x2 ink block at columns 2..3, rows 5..6 of an
//     otherwise empty 6x8 cell, and the glyph pitch is a fixed 6 px.
//   - Roboto (the device path): '.' is a 1x1 dot sitting on the baseline with a
//     3 px advance, so the marker is >=2 single-pixel dots 3 px apart with clean
//     columns between them. A dot must be isolated on ALL FOUR sides: up/down
//     exclude a hyphen (contiguous ink) and the dot of an 'i'/'j' (stem below),
//     and left/right exclude the END of a baseline-terminating stroke — the bottom
//     of 's'/'u'/'n' is a horizontal run whose last pixel is otherwise dot-shaped
//     and, at Roboto's 3 px pitch, lands exactly 3 px from its neighbour (LESSONS 39).
#ifdef CHROMAWOTD_FONT_FREESANS
static bool isIsolatedDot(int x, int y) {
    return g_canvas.getPixel(x, y) != CC_WHITE && g_canvas.getPixel(x, y - 1) == CC_WHITE &&
           g_canvas.getPixel(x, y + 1) == CC_WHITE && g_canvas.getPixel(x - 1, y) == CC_WHITE &&
           g_canvas.getPixel(x + 1, y) == CC_WHITE;
}

static bool hasOverflowMarker(int x0, int y0, int x1, int y1) {
    // The dot's own row must lie inside the region, but its neighbours may be read
    // outside it: CcCanvas::getPixel() is bounds-safe (returns background), which
    // matters because the alert's marker sits on the panel's second-to-last row.
    for (int y = y0; y <= y1; y++)
        for (int cx = x0 + 1; cx + 4 <= x1; cx++)
            if (isIsolatedDot(cx, y) && g_canvas.getPixel(cx + 1, y) == CC_WHITE &&
                g_canvas.getPixel(cx + 2, y) == CC_WHITE && isIsolatedDot(cx + 3, y))
                return true;
    return false;
}
#else
static bool cellIsDotGlyph(int cx, int cy) {
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 6; i++) {
            bool expected = (i >= 2 && i <= 3) && (j >= 5 && j <= 6);
            bool inked = g_canvas.getPixel(cx + i, cy + j) != CC_WHITE;
            if (expected != inked)
                return false;
        }
    }
    return true;
}

static bool hasOverflowMarker(int x0, int y0, int x1, int y1) {
    for (int cy = y0; cy + 8 <= y1; cy++)
        for (int cx = x0; cx + 2 * 6 <= x1; cx++)
            if (cellIsDotGlyph(cx, cy) && cellIsDotGlyph(cx + 6, cy))
                return true;
    return false;
}
#endif

static VerseData verse(const char* text, const char* highlight, const char* reference) {
    return {"Fri, Sep 12", text, highlight, reference};
}

static const char* kLongVerse = "Trust in the Lord with all your heart, and do not lean on your own understanding. "
                                "In all your ways acknowledge him, and he will make straight your paths. Be not wise "
                                "in your own eyes; fear the Lord, and turn away from evil. It will be healing to your "
                                "flesh and refreshment to your bones. Honor the Lord with your wealth and with the "
                                "firstfruits of all your produce; then your barns will be filled with plenty, and your "
                                "vats will be bursting with wine.";

// ------------------------------------------------------- temp rounding -----

TEST(Temperature, NegativeTempsRoundAwayFromZero) {
    WeatherData a = {-0.6f, "Cold", nullptr, WeatherIcon::Cloud};
    WeatherData b = {-1.0f, "Cold", nullptr, WeatherIcon::Cloud};
    WeatherData c = {-0.4f, "Cold", nullptr, WeatherIcon::Cloud};
    WeatherData d = {0.0f, "Cold", nullptr, WeatherIcon::Cloud};

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), a);
    std::string minus1 = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), b);
    EXPECT_EQ(minus1, canvasHash()) << "-0.6 must render as -1";

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), c);
    std::string zero = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), d);
    EXPECT_EQ(zero, canvasHash()) << "-0.4 must render as 0";

    EXPECT_NE(minus1, zero) << "-0.6 and 0.0 must not render identically";
}

TEST(Temperature, PositiveTempsStillRoundUp) {
    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"),
               WeatherData{24.5f, "Mild", nullptr, WeatherIcon::PartlyCloudy});
    std::string r245 = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"),
               WeatherData{25.0f, "Mild", nullptr, WeatherIcon::PartlyCloudy});
    EXPECT_EQ(r245, canvasHash());
}

// ---------------------------------------------------- overflow handling ----

TEST(VerseOverflow, LandscapeMarksOverflowAndKeepsDividerIntact) {
    g_canvas.init(296, 128);
    drawLayout(verse(kLongVerse, nullptr, "Proverbs 3:5-6"),
               WeatherData{24.5f, "Partly cloudy", nullptr, WeatherIcon::PartlyCloudy});

    // Verse region is x 4..218 (splitX=226, minus the 8px wall). The marker must
    // appear inside that region.
    EXPECT_TRUE(hasOverflowMarker(8, 16, 218, 126)) << "long verse must be visibly marked as truncated";
    // The divider is at x=226 and the weather column starts at x=227, so the
    // gutter immediately right of the divider must stay blank. x=230 is NOT part
    // of that gutter: it legitimately carries weather-column content — the
    // condition line ("Partly cloudy") sets on ONE centred line in the
    // proportional font and reaches x=230, where 5x7 wraps it to two lines and
    // stops short (LESSONS 39).
    EXPECT_TRUE(rectAllColor(227, 0, 229, 127, CC_WHITE))
        << "verse text crossed the divider into the weather-column gutter";

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_overflow_landscape.png"));
}

TEST(VerseOverflow, FittingVerseHasNoMarker) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Proverbs 3:5"),
               WeatherData{27.0f, "Sunny", nullptr, WeatherIcon::Sun});

    EXPECT_FALSE(hasOverflowMarker(8, 20, 196, 127)) << "a verse that fits must not be marked as truncated";
}

TEST(WeatherAlert, TruncatedAlertStaysInsideColumn) {
    g_canvas.init(296, 128);
    WeatherData w = {22.0f, "Heavy rain",
                     "Dense fog and black ice expected overnight in low lying areas, exercise caution on untreated "
                     "roads and bridges",
                     WeatherIcon::Rain};
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), w);

    // Alert is pinned to the bottom of the weather column (centrex 261, colW 66);
    // the marker must appear within that column (x 228..294).
    EXPECT_TRUE(hasOverflowMarker(228, 90, 294, 126)) << "over-long alert must be marked as truncated";
    // Nothing may spill off the panel's right edge: x 295 is the last content-free
    // column (wrapped alert text is centred in the 66px weather column, <=x 294).
    EXPECT_TRUE(rectAllColor(295, 0, 295, 127, CC_WHITE)) << "alert text spilled past the right edge of the panel";
}

// ------------------------------------------------------- highlight rules ---

TEST(Highlight, FoundFlagReportsMatchHonestly) {
    EXPECT_TRUE(verseHighlightFound(verse("Hope in the Lord", "the Lord", "Ref")));
    EXPECT_TRUE(verseHighlightFound(verse("Hope in the Lord", "THE LORD", "Ref")))
        << "case-insensitive fallback must be reported as found";
    EXPECT_FALSE(verseHighlightFound(verse("Hope in the Lord", "the Lamb", "Ref")));
    EXPECT_FALSE(verseHighlightFound(verse("Hope in the Lord", nullptr, "Ref")));
}

TEST(Highlight, CaseInsensitiveFallbackStillAccentsRed) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", "THE LORD", "Prov 3:5"),
               WeatherData{20.0f, "Clear", nullptr, WeatherIcon::Sun});
    int redWithHighlight = colorCount(4, 18, 218, 100, CC_RED);

    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Prov 3:5"),
               WeatherData{20.0f, "Clear", nullptr, WeatherIcon::Sun});
    int redWithout = colorCount(4, 18, 218, 100, CC_RED);

    EXPECT_GT(redWithHighlight, redWithout) << "case-mismatched highlight must still be painted in red";
}

TEST(Highlight, MissingHighlightLeavesBodyUnaccented) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", "a phrase not present", "Prov 3:5"),
               WeatherData{20.0f, "Clear", nullptr, WeatherIcon::Sun});

    EXPECT_EQ(colorCount(8, 26, 196, 102, CC_RED), 0) << "body text must not be accented when the phrase is absent";
}

// --------------------------------------------------- unicode normalisation --

TEST(Unicode, TypographicGlyphsRenderAsAsciiEquivalents) {
    const char* typographic = "Then he said, \xE2\x80\x9CLet there be light\xE2\x80\x9D\xE2\x80\x94"
                              "and there was light. "
                              "God\xE2\x80\x99s spirit moved\xE2\x80\xA6 \xE2\x9A\xA0 it was good.";
    const char* asciiEquivalent = "Then he said, \"Let there be light\"-and there was light. "
                                  "God's spirit moved. ! it was good.";

    g_canvas.init(296, 128);
    drawLayout(verse(typographic, nullptr, "Gen 1:3"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    std::string typographicHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:3"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    EXPECT_EQ(typographicHash, canvasHash()) << "typographic UTF-8 must be normalised to ASCII before drawing";
}

TEST(Unicode, NonBreakingSpaceDoesNotBreakLayout) {
    g_canvas.init(296, 128);
    drawLayout(
        verse("Lord\u00C2\u00A0of\u00C2\u00A0hosts, blessed is the one who trusts in you.", "blessed", "Ps 84:12"),
        WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});

    // Divider at x=226 must stay black (no verse text crosses into weather column).
    EXPECT_TRUE(rectAllColor(226, 0, 226, 127, CC_BLACK)) << "divider column must be intact";
}

// Adversarial UTF-8 (S2): truncated / overlong sequences must not be read past
// the declared length bound, and each orphaned byte collapses to one '?'.
//   - "\xE2\x80" (3-byte lead cut mid-grapheme) -> two '?' (lead + orphan continuation)
//   - "\xF0"     (lone 4-byte lead)            -> one '?'
//   - "\xC2"     (lone 2-byte lead)            -> one '?'
TEST(Unicode, TruncatedSequencesCollapseToOneQuestionMarkPerByte) {
    const char* truncated = "a \xE2\x80 b \xF0 c \xC2 d";
    const char* asciiEquivalent = "a ?? b ? c ? d";

    g_canvas.init(296, 128);
    drawLayout(verse(truncated, nullptr, "Gen 1:1"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    std::string truncatedHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:1"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    EXPECT_EQ(truncatedHash, canvasHash())
        << "truncated UTF-8 must collapse each orphaned byte to a '?' (bounded, no OOB read)";
}

// A 4-byte emoji (overlong) maps to one '?' — not one per byte.
TEST(Unicode, FourByteSequenceCollapsesToSingleReplacement) {
    const char* emoji = "joy \xF0\x9F\x98\x80 today";
    const char* asciiEquivalent = "joy ? today";

    g_canvas.init(296, 128);
    drawLayout(verse(emoji, nullptr, "Ps 1:1"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    std::string emojiHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Ps 1:1"), WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});
    EXPECT_EQ(emojiHash, canvasHash()) << "a 4-byte sequence must collapse to a single '?'";
}

// ------------------------------------------------------ weather icon cases --

// Each of the four weather icons must render distinct pixels — proof that
// drawWeatherIcon actually draws per-icon content and a refactor or new size
// can't silently regress one icon into another (C3).
TEST(WeatherIcon, FourIconsAreDistinct) {
    const char* shortVerse = "Trust in the Lord.";
    std::string hashes[4];

    for (int i = 0; i < 4; i++) {
        g_canvas.init(296, 128);
        drawLayout(verse(shortVerse, nullptr, "Prov 3:5"),
                   WeatherData{21.0f, "Clear", nullptr, static_cast<WeatherIcon>(i)});
        hashes[i] = canvasHash();
    }

    // All four hashes must be pairwise distinct.
    for (int a = 0; a < 4; a++) {
        for (int b = a + 1; b < 4; b++) {
            EXPECT_NE(hashes[a], hashes[b]) << "icon " << a << " and icon " << b << " rendered identically";
        }
    }
}

// The weather icon must actually paint pixels in the column region (not be a no-op).
TEST(WeatherIcon, SunIconPaintsYellow) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord.", nullptr, "Prov 3:5"),
               WeatherData{21.0f, "Clear", nullptr, WeatherIcon::Sun});

    // The sun icon (and its rays) are yellow; the weather column (x > 233) must
    // contain yellow ink beyond the FORECAST header.
    int yellow = colorCount(233, 20, 295, 127, CC_YELLOW);
    EXPECT_GT(yellow, 0) << "sun icon must paint yellow pixels in the weather column";
}

// ------------------------------------------------- no-reading honesty ------

// A failed weather fetch must NOT draw a temperature. The struct is zero-initialised
// on the device, so without the `valid` flag the panel confidently rendered "0°C" —
// a fabricated measurement that looks like real data. These tests pin that down.
TEST(NoReading, InvalidWeatherDrawsNoTemperature) {
    g_canvas.init(296, 128);
    WeatherData w{0.0f, nullptr, "OFFLINE: no wifi", WeatherIcon::PartlyCloudy};
    w.valid = false;
    drawLayout(verse("No network connection.", nullptr, nullptr), w);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_noreading.png"));

    // No digit-like black ink anywhere in the weather column now that there is no
    // reading. Scanning the whole column (rather than one y-range) keeps this honest
    // regardless of which layout branch ran — a fabricated 0°C showed up as black
    // glyph ink here, which is exactly what this guards against.
    const int blackInColumn = colorCount(228, 20, 294, 126, CC_BLACK);
    EXPECT_EQ(blackInColumn, 0) << "an invalid reading must not render a temperature (was the 0°C fabrication)";

    // The explicit notice must be present, so the user is told rather than left blank.
    const int noticeInk = colorCount(228, 20, 294, 70, CC_RED);
    EXPECT_GT(noticeInk, 0) << "an invalid reading must state 'No reading'";
}

TEST(NoReading, ValidZeroDegreesStillDrawsAVisibleTemperature) {
    // The counterpart: a GENUINE 0°C reading must still render. This is why the fix is
    // a validity flag and not "treat 0 as missing" — 0°C is a real temperature.
    //
    // Measured range: with no alert the icon grows and the temperature sits at
    // y=71..84 (the alert layout puts it at y=47..60 instead, which is what the
    // invalid-reading test above checks).
    g_canvas.init(296, 128);
    WeatherData w{0.0f, "Clear", nullptr, WeatherIcon::Sun};
    w.valid = true;
    drawLayout(verse("Cold morning.", nullptr, "Ref 1:1"), w);

    const int inkAtTempRow = colorCount(228, 70, 294, 86, CC_BLACK);
    EXPECT_GT(inkAtTempRow, 0) << "a real 0C reading must be displayed";
}
