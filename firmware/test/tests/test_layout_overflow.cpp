// test_layout_overflow.cpp — invariants that a pixel-probe suite cannot catch:
// overflow spill, truncation markers, region containment, temperature rounding,
// highlight robustness and UTF-8 normalisation. All targets the single light,
// landscape layout.
#include "../harness/canvas.h"
#include "verse_display.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------- helpers ---

static std::string canvasHash() {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (uint8_t b : g_canvas.px) { h ^= b; h *= 1099511628211ull; }
    return std::to_string(h);
}

static bool rectAllColor(int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) != color) return false;
    return true;
}

static int colorCount(int x0, int y0, int x1, int y1, uint32_t color) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) == color) n++;
    return n;
}

// The ellipsis overflow marker is made of '.' glyphs. In the vendored 5x7 font '.'
// is a 2x2 ink block at columns 2..3, rows 5..6 of an otherwise empty 6x8 cell, so
// two cell-exact matches at the 6-px glyph pitch identify the marker without false
// positives from ordinary prose.
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
    WeatherData a = { -0.6f, "Cold", nullptr, WeatherIcon::Cloud };
    WeatherData b = { -1.0f, "Cold", nullptr, WeatherIcon::Cloud };
    WeatherData c = { -0.4f, "Cold", nullptr, WeatherIcon::Cloud };
    WeatherData d = {  0.0f, "Cold", nullptr, WeatherIcon::Cloud };

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
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), WeatherData{ 24.5f, "Mild", nullptr, WeatherIcon::PartlyCloudy });
    std::string r245 = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), WeatherData{ 25.0f, "Mild", nullptr, WeatherIcon::PartlyCloudy });
    EXPECT_EQ(r245, canvasHash());
}

// ---------------------------------------------------- overflow handling ----

TEST(VerseOverflow, LandscapeMarksOverflowAndKeepsDividerIntact) {
    g_canvas.init(296, 128);
    drawLayout(verse(kLongVerse, nullptr, "Proverbs 3:5-6"), WeatherData{ 24.5f, "Partly cloudy", nullptr, WeatherIcon::PartlyCloudy });

    // Verse region is x 4..218 (splitX=226, minus the 8px wall). The marker must
    // appear inside that region.
    EXPECT_TRUE(hasOverflowMarker(8, 16, 218, 126))
        << "long verse must be visibly marked as truncated";
    // The divider is at x=226; the weather-column FORECAST rule starts at x=233,
    // so the gutter x=227..230 must stay blank (x=231-232 may have sun icon pixels).
    EXPECT_TRUE(rectAllColor(227, 0, 230, 127, CC_WHITE))
        << "verse text crossed the divider into the weather-column gutter";

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_overflow_landscape.png"));
}

TEST(VerseOverflow, FittingVerseHasNoMarker) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Proverbs 3:5"), WeatherData{ 27.0f, "Sunny", nullptr, WeatherIcon::Sun });

    EXPECT_FALSE(hasOverflowMarker(8, 20, 196, 127))
        << "a verse that fits must not be marked as truncated";
}

TEST(WeatherAlert, TruncatedAlertStaysInsideColumn) {
    g_canvas.init(296, 128);
    WeatherData w = { 22.0f, "Heavy rain",
                      "Dense fog and black ice expected overnight in low lying areas, exercise caution on untreated roads and bridges",
                      WeatherIcon::Rain };
    drawLayout(verse("Short verse.", nullptr, "Ref 1:1"), w);

    // Alert is pinned to the bottom of the weather column (centrex 261, colW 66);
    // the marker must appear within that column (x 228..294).
    EXPECT_TRUE(hasOverflowMarker(228, 90, 294, 126))
        << "over-long alert must be marked as truncated";
    // Nothing may spill off the panel's right edge: x 295 is the last content-free
    // column (wrapped alert text is centred in the 66px weather column, <=x 294).
    EXPECT_TRUE(rectAllColor(295, 0, 295, 127, CC_WHITE))
        << "alert text spilled past the right edge of the panel";
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
    drawLayout(verse("Trust in the Lord with all your heart.", "THE LORD", "Prov 3:5"), WeatherData{ 20.0f, "Clear", nullptr, WeatherIcon::Sun });
    int redWithHighlight = colorCount(4, 18, 218, 100, CC_RED);

    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", nullptr, "Prov 3:5"), WeatherData{ 20.0f, "Clear", nullptr, WeatherIcon::Sun });
    int redWithout = colorCount(4, 18, 218, 100, CC_RED);

    EXPECT_GT(redWithHighlight, redWithout)
        << "case-mismatched highlight must still be painted in red";
}

TEST(Highlight, MissingHighlightLeavesBodyUnaccented) {
    g_canvas.init(296, 128);
    drawLayout(verse("Trust in the Lord with all your heart.", "a phrase not present", "Prov 3:5"),
               WeatherData{ 20.0f, "Clear", nullptr, WeatherIcon::Sun });

    EXPECT_EQ(colorCount(8, 26, 196, 102, CC_RED), 0)
        << "body text must not be accented when the phrase is absent";
}

// --------------------------------------------------- unicode normalisation --

TEST(Unicode, TypographicGlyphsRenderAsAsciiEquivalents) {
    const char* typographic =
        "Then he said, \xE2\x80\x9CLet there be light\xE2\x80\x9D\xE2\x80\x94" "and there was light. "
        "God\xE2\x80\x99s spirit moved\xE2\x80\xA6 \xE2\x9A\xA0 it was good.";
    const char* asciiEquivalent =
        "Then he said, \"Let there be light\"-and there was light. "
        "God's spirit moved. ! it was good.";

    g_canvas.init(296, 128);
    drawLayout(verse(typographic, nullptr, "Gen 1:3"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    std::string typographicHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:3"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    EXPECT_EQ(typographicHash, canvasHash())
        << "typographic UTF-8 must be normalised to ASCII before drawing";
}

TEST(Unicode, NonBreakingSpaceDoesNotBreakLayout) {
    g_canvas.init(296, 128);
    drawLayout(verse("Lord\u00C2\u00A0of\u00C2\u00A0hosts, blessed is the one who trusts in you.", "blessed", "Ps 84:12"),
               WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });

    // Divider at x=226 must stay black (no verse text crosses into weather column).
    EXPECT_TRUE(rectAllColor(226, 0, 226, 127, CC_BLACK))
        << "divider column must be intact";
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
    drawLayout(verse(truncated, nullptr, "Gen 1:1"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    std::string truncatedHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Gen 1:1"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    EXPECT_EQ(truncatedHash, canvasHash())
        << "truncated UTF-8 must collapse each orphaned byte to a '?' (bounded, no OOB read)";
}

// A 4-byte emoji (overlong) maps to one '?' — not one per byte.
TEST(Unicode, FourByteSequenceCollapsesToSingleReplacement) {
    const char* emoji = "joy \xF0\x9F\x98\x80 today";
    const char* asciiEquivalent = "joy ? today";

    g_canvas.init(296, 128);
    drawLayout(verse(emoji, nullptr, "Ps 1:1"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    std::string emojiHash = canvasHash();

    g_canvas.init(296, 128);
    drawLayout(verse(asciiEquivalent, nullptr, "Ps 1:1"), WeatherData{ 21.0f, "Clear", nullptr, WeatherIcon::Sun });
    EXPECT_EQ(emojiHash, canvasHash())
        << "a 4-byte sequence must collapse to a single '?'";
}