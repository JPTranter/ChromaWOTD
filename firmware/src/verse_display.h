// verse_display.h — layout engine for CHROMAWOTD.
// Runs both on-device (Seeed GFX / ESP32-S3) and on PC (test harness with mock canvas).

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

// Legacy aliases
#define C_WHITE  CC_WHITE
#define C_BLACK  CC_BLACK
#define C_RED    CC_RED
#define C_YELLOW CC_YELLOW

void drawLayout(const VerseData& v, const WeatherData& w, bool landscape, bool inverted = false);
// True when v.highlight was located inside v.verse (exact or case-insensitive).
// Callers should surface a false result rather than silently losing the red accent.
bool verseHighlightFound(const VerseData& v);
void drawLayoutPortrait(const VerseData& v, const WeatherData& w);
void drawLayoutPortraitInverted(const VerseData& v, const WeatherData& w);
void drawLayoutLandscape(const VerseData& v, const WeatherData& w);
void drawLayoutLandscapeInverted(const VerseData& v, const WeatherData& w);
void drawLayoutLandscapeDark(const VerseData& v, const WeatherData& w);


