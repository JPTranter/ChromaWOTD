// test_layout_alert.cpp — alert banner placement in the single landscape layout.
#include "../harness/canvas.h"
#include "verse_display.h"
#include <gtest/gtest.h>

// A single canvas column must be uniformly one colour (spill check).
static bool columnAllColor(int x, int y0, int y1, uint32_t color) {
    for (int y = y0; y <= y1; y++)
        if (g_canvas.getPixel(x, y) != color) return false;
    return true;
}

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
    WeatherData w = { 22.0f, "Heavy rain", "Rain after 4 PM", WeatherIcon::Rain };
    drawLayout(sampleVerse(), w);

    // Red right edge of the column, above the alert, stays white canvas (no spill).
    EXPECT_EQ(g_canvas.getPixel(294, 50), CC_WHITE);

    // The alert divider is DYNAMIC: drawLandscapeWeatherColumn() computes its y
    // from the wrapped line count of the alert text (divY = labelY - 3), so the
    // Roboto path wraps "Rain after 4 PM" into fewer lines and pins the rule
    // higher than 5x7 does (101 vs the 94 this used to hardcode). Locate the rule
    // by its actual signature — the red run spanning the caption width
    // (cx +/- kHalf = 28 px, so >=40 px of contiguous red) — and assert it sits
    // BELOW the temperature and above the alert text, instead of probing one
    // pixel whose y depends on the font (LESSONS 39).
    auto redRunInRow = [](int y) {
        int best = 0, run = 0;
        for (int x = 228; x <= 294; x++) {
            run = (g_canvas.getPixel(x, y) == CC_RED) ? run + 1 : 0;
            if (run > best) best = run;
        }
        return best;
    };

    int ruleY = -1;
    for (int y = 60; y < 128; y++) {
        if (redRunInRow(y) >= 40) { ruleY = y; break; }
    }
    EXPECT_GE(ruleY, 0) << "the red alert divider rule is missing from the weather column";

    if (ruleY >= 0) {
        // Above the rule: the temperature block ("22") sits in the column and the
        // temperature row must be red-free (the rule is the first red band down there).
        EXPECT_EQ(redRunInRow(50), 0) << "red ink above the alert divider where only the temp belongs";
        // Below the rule: the red ALERT: caption and the wrapped alert text exist.
        int redBelow = 0;
        for (int y = ruleY + 1; y < 128; y++)
            for (int x = 228; x <= 294; x++)
                if (g_canvas.getPixel(x, y) == CC_RED) redBelow++;
        EXPECT_GT(redBelow, 0) << "alert caption/text must render red below its divider";
        // Nothing may spill off the panel's right/bottom edges.
        EXPECT_TRUE(columnAllColor(295, 0, 127, CC_WHITE))
            << "alert ink reached the panel's last column";
    }

    ASSERT_TRUE(g_canvas.dumpPng("output/layout_alert_landscape.png"));
}