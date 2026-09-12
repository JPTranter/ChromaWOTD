// test_layout_alert.cpp — alert banner placement in the single landscape layout.
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

TEST(LayoutAlert, LandscapeWithAlert) {
    g_canvas.init(296, 128);
    WeatherData w = { 22.0f, "Heavy rain", "Rain after 4 PM", 2 };
    drawLayout(sampleVerse(), w);

    // Red right edge of the column, above the alert, stays white canvas (no spill).
    EXPECT_EQ(g_canvas.getPixel(294, 50), CC_WHITE);

    // Red alert divider line is pinned near the bottom of the view (y = 94).
    EXPECT_EQ(g_canvas.getPixel(251, 94), CC_RED);

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_alert_landscape.png"));
}