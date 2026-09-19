// test_layout_overflow.cpp — invariants that a pixel-probe suite cannot catch:
// overflow spill, truncation markers, region containment, temperature rounding,
// highlight robustness and UTF-8 normalisation. All targets the single light,
// landscape layout.
#include "../harness/canvas.h"
#include "text/date_format.h"
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
    static char date[16];
    // Through the SHARED formatter: the device builds its header date with
    // cc_formatHeaderDate(), so a literal here could render a format the panel never
    // produces (that is exactly how the ISO-vs-"Fri, Sep 12" mismatch slipped through).
    cc_formatHeaderDate(5, 12, 9, date, sizeof(date)); // Friday 12 Sep
    return {date, text, highlight, reference};
}

static const char* kLongVerse = "Trust in the Lord with all your heart, and do not lean on your own understanding. "
                                "In all your ways acknowledge him, and he will make straight your paths. Be not wise "
                                "in your own eyes; fear the Lord, and turn away from evil. It will be healing to your "
                                "flesh and refreshment to your bones. Honor the Lord with your wealth and with the "
                                "firstfruits of all your produce; then your barns will be filled with plenty, and your "
                                "vats will be bursting with wine.";

// ------------------------------------------------------- temp rounding -----

TEST(Temperature, NegativeTempsRoundAwayFromZero) {
    WeatherData a = {-0.6f, "Cold", nullptr};
    WeatherData b = {-1.0f, "Cold", nullptr};
    WeatherData c = {-0.4f, "Cold", nullptr};
    WeatherData d = {0.0f, "Cold", nullptr};

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
               WeatherData{24.5f, "Mild", nullptr});
    std::string r245 = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"),
               WeatherData{25.0f, "Mild", nullptr});
    EXPECT_EQ(r245, canvasHash());
}

// ---------------------------------------------------- overflow handling ----

TEST(VerseOverflow, LandscapeMarksOverflowWithoutSpilling) {
    g_canvas.init(296, 128);
    drawLayout(verse(kLongVerse, nullptr, "Proverbs 3:5-6"),
               WeatherData{24.5f, "Partly cloudy", nullptr});

    // The verse block is x 4..291 (the full panel width less the 4px margins). The marker
    // must appear inside it.
    EXPECT_TRUE(hasOverflowMarker(8, 18, 291, 105))
        << "long verse must be visibly marked as truncated";
    // Nothing may reach the panel's right margin. (There is no divider to cross any more,
    // so the margin is the only boundary left to police.)
    EXPECT_TRUE(rectAllColor(293, 18, 295, 110, CC_WHITE)) << "verse text reached the right margin";

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_overflow_landscape.png"));
}

TEST(VerseOverflow, FittingVerseHasNoMarker) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Proverbs 3:5"),
               WeatherData{27.0f, "Sunny", nullptr});

    EXPECT_FALSE(hasOverflowMarker(8, 20, 196, 127)) << "a verse that fits must not be marked as truncated";
}

TEST(WeatherAlert, TruncatedAlertStaysOnTheFooterRow) {
    g_canvas.init(296, 128);
    WeatherData w = {22.0f, "Heavy rain",
                     "Dense fog and black ice expected overnight in low lying areas, exercise caution on untreated "
                     "roads and bridges"};
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), w);

    // The warning shares the footer row with the weather. An over-long one is truncated
    // with a VISIBLE marker (it used to be drawn unbounded and ran off the panel).
    EXPECT_TRUE(hasOverflowMarker(8, 112, 200, 127))
        << "an over-long warning must be marked as truncated, not silently clipped";
    // The weather text must survive on the right of the same row.
    EXPECT_GT(colorCount(200, 112, 295, 127, CC_BLACK), 0)
        << "the warning overprinted the weather text";
    // Nothing may spill past the right margin, below the header band.
    EXPECT_TRUE(rectAllColor(295, 18, 295, 127, CC_WHITE))
        << "warning text spilled past the right edge of the panel";
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
               WeatherData{20.0f, "Clear", nullptr});
    int redWithHighlight = colorCount(4, 18, 218, 100, CC_RED);

    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Prov 3:5"),
               WeatherData{20.0f, "Clear", nullptr});
    int redWithout = colorCount(4, 18, 218, 100, CC_RED);

    EXPECT_GT(redWithHighlight, redWithout) << "case-mismatched highlight must still be painted in red";
}

TEST(Highlight, MissingHighlightLeavesBodyUnaccented) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", "a phrase not present", "Prov 3:5"),
               WeatherData{20.0f, "Clear", nullptr});

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
    drawLayout(verse(typographic, nullptr, "Gen 1:3"), WeatherData{21.0f, "Clear", nullptr});
    std::string typographicHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:3"), WeatherData{21.0f, "Clear", nullptr});
    EXPECT_EQ(typographicHash, canvasHash()) << "typographic UTF-8 must be normalised to ASCII before drawing";
}

TEST(Unicode, NonBreakingSpaceRendersExactlyLikeAPlainSpace) {
    // The invariant that matters: a NBSP normalises to a space, so the render must be
    // byte-identical to the same text typed with plain spaces. (The old assertion checked
    // a divider column that no longer exists.)
    g_canvas.init(296, 128);
    drawLayout(
        // NBSP as RAW BYTES (0xC2 0xA0). A \u00C2\u00A0 escape would encode U+00C2
        // followed by U+00A0 — "A-hat" plus a NBSP — which is not the same text at all.
        verse("Lord\xC2\xA0of\xC2\xA0hosts, blessed is the one who trusts in you.", "blessed", "Ps 84:12"),
        WeatherData{21.0f, "Clear", nullptr});
    const std::string withNbsp = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Lord of hosts, blessed is the one who trusts in you.", "blessed", "Ps 84:12"),
               WeatherData{21.0f, "Clear", nullptr});
    EXPECT_EQ(withNbsp, canvasHash()) << "NBSP must render exactly as a plain space";
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
    drawLayout(verse(truncated, nullptr, "Gen 1:1"), WeatherData{21.0f, "Clear", nullptr});
    std::string truncatedHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:1"), WeatherData{21.0f, "Clear", nullptr});
    EXPECT_EQ(truncatedHash, canvasHash())
        << "truncated UTF-8 must collapse each orphaned byte to a '?' (bounded, no OOB read)";
}

// A 4-byte emoji (overlong) maps to one '?' — not one per byte.
TEST(Unicode, FourByteSequenceCollapsesToSingleReplacement) {
    const char* emoji = "joy \xF0\x9F\x98\x80 today";
    const char* asciiEquivalent = "joy ? today";

    g_canvas.init(296, 128);
    drawLayout(verse(emoji, nullptr, "Ps 1:1"), WeatherData{21.0f, "Clear", nullptr});
    std::string emojiHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Ps 1:1"), WeatherData{21.0f, "Clear", nullptr});
    EXPECT_EQ(emojiHash, canvasHash()) << "a 4-byte sequence must collapse to a single '?'";
}

// ------------------------------------------------- no-reading honesty ------

// A failed weather fetch must NOT draw a temperature. The struct is zero-initialised
// on the device, so without the `valid` flag the panel confidently rendered "0°C" —
// a fabricated measurement that looks like real data. These tests pin that down.
TEST(NoReading, InvalidWeatherDrawsNoWeatherText) {
    g_canvas.init(296, 128);
    WeatherData w{0.0f, nullptr, "OFFLINE: no wifi"};
    w.valid = false;
    drawLayout(verse("No network connection.", nullptr, nullptr), w);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_noreading.png"));

    // No weather text on the right of the footer row: the struct is zero-initialised, so
    // without the `valid` flag the panel confidently rendered a fabricated "0°C".
    EXPECT_EQ(colorCount(150, 112, 295, 127, CC_BLACK), 0)
        << "an invalid reading must not render a temperature or condition";

    // The failure is still explained, in red, on the left of the same row.
    EXPECT_GT(colorCount(4, 112, 150, 127, CC_RED), 0) << "an invalid reading must still say why";
}

TEST(NoReading, ValidZeroDegreesStillDrawsAVisibleTemperature) {
    // The counterpart: a GENUINE 0°C reading must still render. This is why the fix is a
    // validity flag and not "treat 0 as missing" — 0°C is a real temperature.
    g_canvas.init(296, 128);
    WeatherData w{0.0f, "Clear", nullptr};
    w.valid = true;
    drawLayout(verse("Cold morning.", nullptr, "Ref 1:1"), w);

    EXPECT_GT(colorCount(150, 112, 295, 127, CC_BLACK), 0) << "a real 0C reading must be displayed";
}
