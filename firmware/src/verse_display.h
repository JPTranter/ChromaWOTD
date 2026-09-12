// verse_display.h — layout engine for CHROMAWOTD.
// Single light layout on a landscape 296x128 panel. Runs both on-device
// (Seeed GFX / ESP32-S3) and on PC (test harness with mock canvas).
//
// Orientation and theme are NOT parameters: the display has exactly one
// presentation — landscape, light theme. Colour is carried by the CC_* constants
// and the palette never varies, so there is nothing to switch on.

#pragma once
#include <cstdint>

struct VerseData {
    const char* date;
    const char* verse;
    const char* highlight;   // phrase shown in red, or nullptr
    const char* reference;
};

struct WeatherData {
    float temp;
    const char* condition;
    const char* alert;       // shown in red, or nullptr
    // icon: 0=sun, 1=cloud, 2=rain, 3=partly
    int icon;
};

// Semantic ePaper colors
#define CC_WHITE   0u
#define CC_BLACK   1u
#define CC_RED     2u
#define CC_YELLOW  3u

// Render the one and only layout (landscape, light theme) into the target canvas.
void drawLayout(const VerseData& v, const WeatherData& w);

// True when v.highlight was located inside v.verse (exact or case-insensitive).
// Callers should surface a false result rather than silently losing the red accent.
bool verseHighlightFound(const VerseData& v);