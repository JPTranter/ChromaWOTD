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
    return { 24.5f, "Partly cloudy", nullptr, 3 };
}

TEST(LayoutLandscape, DividerAndPngDump) {
    g_canvas.init(296, 128);
    drawLayoutLandscape(sampleVerse(), sampleWeather());

    // Header band at (2, 2) must be yellow
    EXPECT_EQ(g_canvas.getPixel(2, 2), CC_YELLOW);

    // Vertical divider line at x = 205
    EXPECT_EQ(g_canvas.getPixel(205, 50), CC_BLACK);

    // Weather column at (250, 100) must be white background
    EXPECT_EQ(g_canvas.getPixel(250, 100), CC_WHITE);

    // Dump PNG
    ASSERT_TRUE(g_canvas.dumpPng("output/layout_landscape.png"));
}

TEST(LayoutLandscape, InvertedPngDump) {
    g_canvas.init(296, 128);
    WeatherData alertWeather = { 24.5f, "Partly cloudy", "Rain likely after 4 PM", 3 };
    drawLayoutLandscapeInverted(sampleVerse(), alertWeather);

    // Header band at (2, 2) is yellow
    EXPECT_EQ(g_canvas.getPixel(2, 2), CC_YELLOW);

    // Background in verse body (50, 50) is black
    EXPECT_EQ(g_canvas.getPixel(50, 50), CC_BLACK);

    // Vertical divider line at x = 205 is white
    EXPECT_EQ(g_canvas.getPixel(205, 50), CC_WHITE);

    // Weather column background at (250, 100) is black
    EXPECT_EQ(g_canvas.getPixel(250, 100), CC_BLACK);

    // Dump PNG
    ASSERT_TRUE(g_canvas.dumpPng("output/layout_landscape_inverted.png"));
}

TEST(LayoutLandscape, DarkPngDump) {
    g_canvas.init(296, 128);
    WeatherData alertWeather = { 24.5f, "Partly cloudy", "Rain likely after 4 PM", 3 };
    drawLayoutLandscapeDark(sampleVerse(), alertWeather);

    // Header area background at (2, 2) is black
    EXPECT_EQ(g_canvas.getPixel(2, 2), CC_BLACK);

    // Vertical divider line at x = 205 is yellow
    EXPECT_EQ(g_canvas.getPixel(205, 50), CC_YELLOW);

    // Dump PNG
    ASSERT_TRUE(g_canvas.dumpPng("output/layout_landscape_dark.png"));
}
