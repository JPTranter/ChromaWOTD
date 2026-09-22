// test_layout_landscape.cpp — geometry and PNG dump for the single (light,
// landscape) layout.
//
// The layout gives the WHOLE panel to the verse: there is no weather column and no
// divider. The header band carries WHAT you are reading (the citation, or the word with
// its respelling) plus the date; a footer row carries status/warnings on the left and the
// weather as TEXT on the right.
#include "../harness/canvas.h"
#include "text/date_format.h"
#include "verse_display.h"
#include <gtest/gtest.h>

// Layout constants, kept here so this test tracks the real geometry (see drawLayout):
//   header band height = 18 px   (tall enough for the 7pt identity line)
//   old divider x      = 226     (where the removed weather column used to start)
static const int kHeaderH      = 18;
static const int kRightMarginX = 295; // the last column any content may touch
static const int kOldDividerX  = 226;

static VerseData sampleVerse() {
    static char date[16];
    // Through the SHARED formatter: the device builds its header date with
    // cc_formatHeaderDate(), so a literal here could render a format the panel never
    // produces (that is exactly how the ISO-vs-"Fri, Sep 12" mismatch slipped through).
    cc_formatHeaderDate(5, 12, 9, date, sizeof(date)); // Friday 12 Sep
    return {
        date,
        "Trust in the Lord with all your heart, and do not lean on your own understanding. In all your ways acknowledge him, and he will make straight your paths.",
        "he will make straight your paths",
        "Proverbs 3:5-6"
    };
}

static WeatherData sampleWeather() {
    return { 24.5f, "Partly cloudy", nullptr };
}

TEST(LayoutLandscape, NoDivider_FullWidthVerse_PngDump) {
    g_canvas.init(296, 128);
    drawLayout(sampleVerse(), sampleWeather());

    // The header band is yellow and ends exactly at kHeaderH (its last row carries the
    // black separator rule, so probe one row above it).
    EXPECT_EQ(g_canvas.getPixel(2, 2), CC_YELLOW);
    EXPECT_EQ(g_canvas.getPixel(2, kHeaderH - 2), CC_YELLOW);
    EXPECT_EQ(g_canvas.getPixel(2, kHeaderH), CC_WHITE) << "the band must end at kHeaderH";

    // There is NO vertical divider any more: no column is black for the full body height.
    int fullHeightBlack = 0;
    for (int x = 0; x < 296; x++) {
        bool all = true;
        for (int y = kHeaderH; y < 128 && all; y++)
            all = (g_canvas.getPixel(x, y) == CC_BLACK);
        if (all)
            fullHeightBlack++;
    }
    EXPECT_EQ(fullHeightBlack, 0) << "the weather-column divider must be gone";

    // ...and the verse now USES the space it was denied: body ink exists to the right of
    // where the divider used to be. This is the point of the redesign, so assert it
    // positively rather than only asserting the divider is absent.
    int inkRightOfOldDivider = 0;
    for (int y = kHeaderH + 2; y < 105; y++)
        for (int x = kOldDividerX + 1; x <= 292; x++)
            if (g_canvas.getPixel(x, y) != CC_WHITE)
                inkRightOfOldDivider++;
    EXPECT_GT(inkRightOfOldDivider, 0)
        << "the verse must occupy the full panel width, not stop at the old divider";

    // Nothing may reach the panel's right edge. Start BELOW the header band: the band now
    // spans the full width, so it legitimately covers the last column.
    for (int y = kHeaderH; y < 128; y++)
        EXPECT_EQ(g_canvas.getPixel(kRightMarginX, y), CC_WHITE)
            << "right edge pixel at y=" << y << " must stay white";

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_landscape.png"));
}

// The identity line must NEVER lose the respelling. The previous rule dropped it whenever
// the word + respelling did not fit beside the date — 3 of the 18 most recent
// A.Word.A.Day entries, including the word that was on the panel when the user reported
// "not seeing the pronunciation". Asserted as the RELATION between two renders (with and
// without the respelling) rather than as a glyph position, so it holds on both font paths.
// NOTE: the case only REPRODUCES on the device font path (the 7pt identity line measures
// this pair 212px against 208px of room); on the 5x7 fallback path the pair fits and the
// old rule never fired. Verified to FAIL here with the old drop rule restored — the
// decisive run is `verify_all.py` stage 3 (CHROMAWOTD_DEVICE_FONTS=ON).
TEST(LayoutLandscape, RespellingIsNeverDroppedFromTheHeader) {
    static char date[16];
    cc_formatHeaderDate(1, 21, 9, date, sizeof(date)); // Monday 21 Sep
    // 33 chars on one line: this pair does NOT fit at the 7pt identity size beside the date
    // (measured 212px of the 208px available), which is exactly the case the old code
    // handled by dropping the pronunciation.
    VerseData v{date, "adjective: Inducing sleep. The visiting preacher was a grey-haired woman.", nullptr,
                "soporiferous"};

    LayoutOptions withResp;
    withResp.leftCaption = "(sop-uh-RIF-uhr-uhs)";

    auto snapshot = [&](const LayoutOptions& opts, std::vector<uint32_t>* band, std::vector<uint32_t>* body) {
        g_canvas.init(296, 128);
        drawLayout(v, sampleWeather(), opts);
        band->clear();
        body->clear();
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 296; x++)
                ((y < kHeaderH) ? *band : *body).push_back(g_canvas.getPixel(x, y));
    };

    std::vector<uint32_t> bandA, bodyA, bandB, bodyB;
    snapshot(withResp, &bandA, &bodyA);
    snapshot(LayoutOptions{}, &bandB, &bodyB);

    size_t bandDiff = 0;
    for (size_t i = 0; i < bandA.size(); i++)
        if (bandA[i] != bandB[i])
            bandDiff++;
    EXPECT_GT(bandDiff, 0u) << "the respelling must be drawn in the header band, not dropped";

    // ...and it must be the ONLY difference: the body is unaffected by a presentation label.
    EXPECT_EQ(bodyA, bodyB) << "the respelling must not change the body";
}

// The verse box must stop SHORT of the footer row (kRowY = 115). Long text makes the block fill
// every line it is allowed, so this is the worst case: if the box is grown naively, the largest
// rungs put a line's ink into the footer (a 10pt 4th line reaches y=126; a 6pt 6th line reaches
// y=113 and would sit under the weather text). The 2026-09-22 box came within one pixel of the
// footer's top row, so the guard is the 5 rows above it.
TEST(LayoutLandscape, BodyInkNeverReachesTheFooterRow) {
    static std::string longText;
    for (int i = 0; i < 60; i++)
        longText += "line " + std::to_string(i) + " of a body long enough to fill every line the block allows. ";

    static char date[16];
    cc_formatHeaderDate(2, 22, 9, date, sizeof(date)); // Tuesday 22 Sep
    VerseData v{date, longText.c_str(), nullptr, nullptr};

    g_canvas.init(296, 128);
    drawLayout(v, sampleWeather());

    for (int y = 110; y < 115; y++)
        for (int x = 0; x < 296; x++)
            EXPECT_EQ(g_canvas.getPixel(x, y), CC_WHITE)
                << "body ink reached the footer row: x=" << x << " y=" << y;
}
