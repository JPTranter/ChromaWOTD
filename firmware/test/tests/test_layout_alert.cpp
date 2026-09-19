// test_layout_alert.cpp — where a status / weather warning appears.
//
// Warnings (OFFLINE / PARTIAL, or a severe-weather warning) and the weather text share ONE
// footer row: the warning at the left in red, the weather at the right in black. Neither
// may spill off the panel, and an over-long warning must be VISIBLY truncated instead of
// running off the edge — it used to be drawn unbounded.
#include "../harness/canvas.h"
#include "text/date_format.h"
#include "verse_display.h"
#include <gtest/gtest.h>

static int colorCount(int x0, int y0, int x1, int y1, uint32_t color) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_canvas.getPixel(x, y) == color)
                n++;
    return n;
}

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

// The footer row: warnings left in red, weather right in black.
TEST(LayoutAlert, WarningBottomLeft_WeatherBottomRight_PngDump) {
    g_canvas.init(296, 128);
    WeatherData w = { 22.0f, "Heavy rain", "Rain after 4 PM", WeatherIcon::Rain };
    drawLayout(sampleVerse(), w);

    EXPECT_GT(colorCount(4, 112, 150, 127, CC_RED), 0) << "a warning must render bottom-LEFT in red";
    EXPECT_GT(colorCount(180, 112, 295, 127, CC_BLACK), 0) << "the weather text must render bottom-right";

    // The weather is not a warning, so the right half of the row must hold no red...
    EXPECT_EQ(colorCount(200, 112, 295, 127, CC_RED), 0)
        << "red on the right half would mean the weather was styled as a warning";

    // ...and nothing may spill off the panel's last column, below the header band (which
    // spans the full width by design).
    for (int y = 18; y < 128; y++)
        EXPECT_EQ(g_canvas.getPixel(295, y), CC_WHITE)
            << "content reached the panel edge at y=" << y;

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_alert_landscape.png"));
}

// A long warning must be truncated with a VISIBLE marker rather than running off the panel
// or overprinting the weather. Regression: the footer drew the alert unbounded.
TEST(LayoutAlert, OverlongWarningIsVisiblyTruncatedAndStaysInBounds) {
    g_canvas.init(296, 128);
    WeatherData w = { 22.0f,
                      "Heavy rain",
                      "Dense fog and black ice expected overnight in low lying areas, exercise "
                      "caution on untreated roads and bridges",
                      WeatherIcon::Rain };
    drawLayout(sampleVerse(), w);

    // Truncation is marked with "..." — locate it by its SIGNATURE (isolated dots) rather
    // than by a fixed coordinate, since the marker's x depends on the text width.
    int markerX = -1;
    for (int x = 6; x < 290 && markerX < 0; x++) {
        bool isolated = g_canvas.getPixel(x, 119) != CC_WHITE &&
                        g_canvas.getPixel(x - 1, 119) == CC_WHITE &&
                        g_canvas.getPixel(x + 1, 119) == CC_WHITE;
        if (isolated && g_canvas.getPixel(x + 3, 119) != CC_WHITE)
            markerX = x;
    }
    EXPECT_GE(markerX, 0) << "an over-long warning must be marked as truncated";

    // The weather text must still be readable on the right, not overprinted.
    EXPECT_GT(colorCount(180, 112, 295, 127, CC_BLACK), 0)
        << "the warning overprinted the weather text";

    // And nothing may reach the panel's last column below the band.
    for (int y = 18; y < 128; y++)
        EXPECT_EQ(g_canvas.getPixel(295, y), CC_WHITE) << "warning text spilled off the panel edge";
}
