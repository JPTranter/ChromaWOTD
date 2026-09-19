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
