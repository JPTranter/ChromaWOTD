// test_layout_landscape.cpp — geometry and PNG dump for the single (light,
// landscape) layout.
#include "../harness/canvas.h"
#include "verse_display.h"
#include <gtest/gtest.h>

// Layout constants, kept in one place so this test tracks the real geometry:
//   splitX (divider)            = 226   (see drawLayout in verse_display.cpp)
//   weather column centre        = (226 + 1 + 296) / 2 = 261
//   weather column half width    = 28    (FORECAST rule spans 233..289)
//   wrapped alert col width      = 66    (centered text may reach ~x=294)
static const int kDividerX     = 226;
static const int kRightMarginX = 295;     // the last column any content may touch

static VerseData sampleVerse() {
    return {
        "Fri, Sep 12",
        "Trust in the Lord with all your heart, and do not lean on your own understanding. In all your ways acknowledge him, and he will make straight your paths.",
        "he will make straight your paths",
        "Proverbs 3:5-6"
    };
}

static WeatherData sampleWeather() {
    return { 24.5f, "Partly cloudy", nullptr, WeatherIcon::PartlyCloudy };
}

TEST(LayoutLandscape, Divider_PngDump_And_RightMargin) {
    g_canvas.init(296, 128);
    drawLayout(sampleVerse(), sampleWeather());

    // Header band at (2, 2) must be yellow.
    EXPECT_EQ(g_canvas.getPixel(2, 2), CC_YELLOW);

    // The vertical divider is a solid black column at x = 226, full height.
    for (int y = 0; y < 128; y++) {
        EXPECT_EQ(g_canvas.getPixel(kDividerX, y), CC_BLACK)
            << "divider column x=" << kDividerX << " must be black at y=" << y;
    }

    // Nothing may reach the panel's right edge (no content spills past x 294).
    for (int y = 0; y < 128; y += 8) {
        EXPECT_EQ(g_canvas.getPixel(kRightMarginX, y), CC_WHITE)
            << "right edge pixel at x=" << kRightMarginX << ", y=" << y << " must stay white";
    }

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_landscape.png"));
}