// test_layout_overflow.cpp — invariants that the original pixel-probe suites
// could not catch: overflow spill, truncation markers, region containment,
// temperature rounding, highlight robustness and UTF-8 normalisation.
#include "../harness/canvas.h"
#include "verse_display.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------- helpers ---

// Full-canvas fingerprint: layout changes that the eye would notice always move
// this value, which makes "A must render exactly like B" assertions possible.
static std::string canvasHash() {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (uint8_t b : g_canvas.px) { h ^= b; h *= 1099511628211ull; }
    return std::to_string(h);
}

static int inkCount(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) != CC_WHITE) n++;
    return n;
}

static bool rectAllColor(int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) != color) return false;
    return true;
}

// The ellipsis overflow marker is three '.' glyphs. In the vendored 5x7 font '.'
// is a 2x2 ink block at columns 2..3, rows 5..6 of an otherwise empty 6x8 cell, so
// a cell-exact match three times at the 6-px glyph pitch identifies the marker
// without false positives from ordinary prose.
static bool cellIsDotGlyph(int cx, int cy) {
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 6; i++) {
            bool expected = (i >= 2 && i <= 3) && (j >= 5 && j <= 6);
            bool inked = g_canvas.getPixel(cx + i, cy + j) != CC_WHITE;
            if (expected != inked) return false;
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

static int colorCount(int x0, int y0, int x1, int y1, uint32_t color) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) == color) n++;
    return n;
}

static VerseData verse(const char* text, const char* highlight, const char* reference) {
    return { "Fri, Sep 12", text, highlight, reference };
}

static const char* kLongVerse =
    "Trust in the Lord with all your heart, and do not lean on your own understanding. "
    "In all your ways acknowledge him, and he will make straight your paths. Be not wise "
    "in your own eyes; fear the Lord, and turn away from evil. It will be healing to your "
    "flesh and refreshment to your bones. Honor the Lord with your wealth and with the "
    "firstfruits of all your produce; then your barns will be filled with plenty, and your "
    "vats will be bursting with wine.";

// ------------------------------------------------------- temp rounding -----

TEST(Temperature, NegativeTempsRoundAwayFromZero) {
    WeatherData a = { -0.6f, "Cold", nullptr, 1 };
    WeatherData b = { -1.0f, "Cold", nullptr, 1 };
    WeatherData c = { -0.4f, "Cold", nullptr, 1 };
    WeatherData d = {  0.0f, "Cold", nullptr, 1 };

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), a);
    std::string minus1 = canvasHash();

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), b);
    EXPECT_EQ(minus1, canvasHash()) << "-0.6 must render as -1";

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), c);
    std::string zero = canvasHash();

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), d);
    EXPECT_EQ(zero, canvasHash()) << "-0.4 must render as 0";

    EXPECT_NE(minus1, zero) << "-0.6 and 0.0 must not render identically";
}

TEST(Temperature, PositiveTempsStillRoundUp) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), WeatherData{ 24.5f, "Mild", nullptr, 1 });
    std::string r245 = canvasHash();

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), WeatherData{ 25.0f, "Mild", nullptr, 1 });
    EXPECT_EQ(r245, canvasHash());
}

// ---------------------------------------------------- overflow handling ----

TEST(VerseOverflow, PortraitMarksOverflowAndStaysInsideBlock) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse(kLongVerse, nullptr, "Proverbs 3:5-6"), WeatherData{ 27.0f, "Partly cloudy", nullptr, 3 });

    // The verse block is x 6..122, y 28..200: the marker must appear inside it.
    EXPECT_TRUE(hasOverflowMarker(6, 28, 122, 199))
        << "long verse must be visibly marked as truncated";

    // Nothing may spill below the verse block into the reference / weather area.
    EXPECT_TRUE(rectAllColor(0, 220, 127, 243, CC_WHITE))
        << "verse text spilled past its block";

    // The reference rule and weather strip must still be intact.
    EXPECT_EQ(g_canvas.getPixel(14, 204), CC_RED);
    EXPECT_EQ(g_canvas.getPixel(20, 244), CC_BLACK);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_overflow_portrait.png"));
}

TEST(VerseOverflow, FittingVerseHasNoMarker) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Trust in the Lord with all your heart.", nullptr, "Proverbs 3:5"), WeatherData{ 27.0f, "Sunny", nullptr, 0 });

    EXPECT_FALSE(hasOverflowMarker(6, 28, 122, 199))
        << "a verse that fits must not be marked as truncated";
}

TEST(VerseOverflow, LandscapeKeepsDividerIntact) {
    g_canvas.init(296, 128);
    drawLayoutLandscape(verse(kLongVerse, nullptr, "Proverbs 3:5-6"), WeatherData{ 24.5f, "Partly cloudy", nullptr, 3 });

    EXPECT_TRUE(hasOverflowMarker(8, 20, 196, 103))
        << "long landscape verse must be visibly marked as truncated";
    EXPECT_TRUE(rectAllColor(205, 0, 205, 127, CC_BLACK))
        << "verse text crossed the divider column";
    EXPECT_TRUE(rectAllColor(206, 112, 295, 127, CC_WHITE))
        << "weather column bottom must stay clear when there is no alert";

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_overflow_landscape.png"));
}

TEST(WeatherAlert, TruncatedAlertStaysInsideStrip) {
    g_canvas.init(128, 296);
    WeatherData w = { 22.0f, "Heavy rain",
                      "Severe thunderstorm warning with damaging winds and large hail expected across the district this evening",
                      2 };
    drawLayoutPortrait(verse("Short verse.", nullptr, "Ref 1:1"), w);

    EXPECT_TRUE(hasOverflowMarker(44, 284, 124, 295))
        << "over-long alert must be marked as truncated";
    EXPECT_TRUE(rectAllColor(125, 284, 127, 295, CC_WHITE))
        << "alert text spilled past the right edge of the column";
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
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Trust in the Lord with all your heart.", "THE LORD", "Prov 3:5"), WeatherData{ 20.0f, "Clear", nullptr, 0 });
    int redWithHighlight = colorCount(6, 28, 122, 199, CC_RED);

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Trust in the Lord with all your heart.", nullptr, "Prov 3:5"), WeatherData{ 20.0f, "Clear", nullptr, 0 });
    int redWithout = colorCount(6, 28, 122, 199, CC_RED);

    EXPECT_GT(redWithHighlight, redWithout)
        << "case-mismatched highlight must still be painted in red";
}

TEST(Highlight, MissingHighlightLeavesBodyUnaccented) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Trust in the Lord with all your heart.", "a phrase not present", "Prov 3:5"),
                       WeatherData{ 20.0f, "Clear", nullptr, 0 });

    int red = 0;
    for (int y = 28; y <= 199; y++)
        for (int x = 6; x <= 122; x++)
            if (g_canvas.getPixel(x, y) == CC_RED) red++;

    EXPECT_EQ(red, 0) << "body text must not be accented when the phrase is absent";
}

// --------------------------------------------------- unicode normalisation --

TEST(Unicode, TypographicGlyphsRenderAsAsciiEquivalents) {
    const char* typographic =
        "Then he said, \xE2\x80\x9CLet there be light\xE2\x80\x9D\xE2\x80\x94" "and there was light. "
        "God\xE2\x80\x99s spirit moved\xE2\x80\xA6 \xE2\x9A\xA0 it was good.";
    const char* asciiEquivalent =
        "Then he said, \"Let there be light\"-and there was light. "
        "God's spirit moved. ! it was good.";

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse(typographic, nullptr, "Gen 1:3"), WeatherData{ 21.0f, "Clear", nullptr, 0 });
    std::string typographicHash = canvasHash();

    g_canvas.init(128, 296);
    drawLayoutPortrait(verse(asciiEquivalent, nullptr, "Gen 1:3"), WeatherData{ 21.0f, "Clear", nullptr, 0 });
    EXPECT_EQ(typographicHash, canvasHash())
        << "typographic UTF-8 must be normalised to ASCII before drawing";
}

TEST(Unicode, NonBreakingSpaceDoesNotBreakLayout) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(verse("Lord\xC2\xA0of\xC2\xA0hosts, blessed is the one who trusts in you.", "blessed", "Ps 84:12"),
                       WeatherData{ 21.0f, "Clear", nullptr, 0 });

    EXPECT_TRUE(rectAllColor(0, 220, 127, 243, CC_WHITE));
}
