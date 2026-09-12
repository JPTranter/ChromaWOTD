#pragma once
#include <cstdint>

// Semantic ePaper colors
#define CC_WHITE   0u
#define CC_BLACK   1u
#define CC_RED     2u
#define CC_YELLOW  3u

// Weather icon codes (shared by header, layout, render preview, and HTML mockup).
// The mapping 0=sun, 1=cloud, 2=rain, 3=partly is the single source of truth; string
// tables in layout_render.cpp and verse_template.html derive from these values.
enum class WeatherIcon : int {
    Sun          = 0,
    Cloud        = 1,
    Rain         = 2,
    PartlyCloudy = 3,
};

// Orientation and theme are NOT parameters: the display has exactly one presentation —
// landscape, light theme. These enums exist so the dispatcher (drawLayout) can be
// extended to other presentations without magic numbers, and so the README table
// ("which theme works where") is not inferred by the reader.
enum class Orientation { Landscape };
enum class Theme { Light };

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
    WeatherIcon icon;        // 0=sun, 1=cloud, 2=rain, 3=partly
};

// Render the one and only layout (landscape, light theme) into the target canvas.
// The (Orientation, Theme) pair is currently (Landscape, Light) only; the dispatcher
// is written to accept the full enum set so adding a new presentation is mechanical.
void drawLayout(const VerseData& v, const WeatherData& w);

// True when v.highlight was located inside v.verse (exact or case-insensitive).
// Callers should surface a false result rather than silently losing the red accent.
bool verseHighlightFound(const VerseData& v);