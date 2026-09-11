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

static WeatherData sampleWeatherAlert() {
    return { 22.0f, "Heavy rain", "Rain after 4 PM", 2 };
}

TEST(LayoutAlert, PortraitWithAlert) {
    g_canvas.init(128, 296);
    drawLayoutPortrait(sampleVerse(), sampleWeatherAlert());

    // Red alert text is present at y >= 284
    EXPECT_EQ(g_canvas.getPixel(44, 284), CC_RED);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_alert_portrait.png"));
}

TEST(LayoutAlert, LandscapeWithAlert) {
    g_canvas.init(296, 128);
    drawLayoutLandscape(sampleVerse(), sampleWeatherAlert());

    // Red right-edge stripe is removed (right edge at (294, 50) is white canvas)
    EXPECT_EQ(g_canvas.getPixel(294, 50), CC_WHITE);

    // Red alert divider line at x = 220, y = 78
    EXPECT_EQ(g_canvas.getPixel(220, 78), CC_RED);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_alert_landscape.png"));
}
