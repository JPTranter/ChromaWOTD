#include "../harness/canvas.h"
#include "verse_display.h"
#include <gtest/gtest.h>

static VerseData sampleVerse() {
    return {
        "Fri, Sep 12",
        "Trust in the Lord with all your heart, and do not lean on your own understanding. In all your ways acknowledge him, and he will make straight your paths.",
        "he will make straight your paths",
        "Proverbs 3:5-6"
    };
}

static WeatherData sampleWeather() {
    return { 27.0f, "Partly cloudy", nullptr, 3 };
}

TEST(LayoutPortrait, HeaderBandYellowAndPngDump) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(sampleVerse(), sampleWeather());

    // Header band at (10, 10) must be yellow
    EXPECT_EQ(g_canvas.getPixel(10, 10), CC_YELLOW);

    // Verse area at (64, 100) must have white background
    EXPECT_EQ(g_canvas.getPixel(64, 100), CC_WHITE);

    // Weather area at (20, 245) must be black divider
    EXPECT_EQ(g_canvas.getPixel(20, 244), CC_BLACK);

    // Dump PNG
    ASSERT_TRUE(g_canvas.dumpPng("output/layout_portrait.png"));
}
