#pragma once
#include <cstdint>

// Semantic ePaper colors
#define CC_WHITE 0u
#define CC_BLACK 1u
#define CC_RED 2u
#define CC_YELLOW 3u

// Weather icon codes (shared by header, layout, render preview, and HTML mockup).
// The mapping 0=sun, 1=cloud, 2=rain, 3=partly is the single source of truth; string
// tables in layout_render.cpp and verse_template.html derive from these values.
enum class WeatherIcon : int {
    Sun = 0,
    Cloud = 1,
    Rain = 2,
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
    const char* highlight; // phrase shown in red, or nullptr
    const char* reference;
};

struct WeatherData {
    float temp;
    const char* condition;
    const char* alert; // shown in red, or nullptr
    WeatherIcon icon;  // 0=sun, 1=cloud, 2=rain, 3=partly
};

// Optional presentation strings for the single landscape layout. Grouped in a
// struct so adding another label is one field, not another positional argument.
struct LayoutOptions {
    // Yellow-band title ("Verse of the Day" / "Word of the Day").
    const char* headerTitle = "Verse of the Day";
    // Caption above the weather column ("FORECAST" / "TOMORROW").
    const char* weatherLabel = "FORECAST";
    // Black caption drawn at the LEFT end of the bottom rule — used for the
    // Word-of-the-Day pronunciation respelling, e.g. "(bre-VIL-uh-kwuhnt)".
    // Null means nothing is drawn on the left.
    const char* leftCaption = nullptr;
};

// Render the one and only layout (landscape, light theme) into the target canvas.
// The (Orientation, Theme) pair is currently (Landscape, Light) only; the dispatcher
// is written to accept the full enum set so adding a new presentation is mechanical.
// `verseHighlightFound()` is called by the caller, not here.
void drawLayout(const VerseData& v, const WeatherData& w, const LayoutOptions& opts = {});

// True when v.highlight was located inside v.verse (exact or case-insensitive).
// Callers should surface a false result rather than silently losing the red accent.
bool verseHighlightFound(const VerseData& v);

// Which body font the verse-block auto-size ladder selects for `verse` in a
// block of maxW x maxH px (the layout's kVerseMaxW / kVerseMaxH). Under
// CHROMAWOTD_FONT_FREESANS this is the real Roboto 6/5.5/5pt selection; without
// the flag the ladder is compiled out and the fixed default (5.5pt) is reported.
enum class VerseFontSize { Pt5, Pt55, Pt6 };
VerseFontSize cc_verseFontSize(const char* verse, int maxW, int maxH);

// Verse text-block geometry — single source of truth shared by drawLayout()
// and the auto-size tests (kSplitX 226 - 2 * kVerseMargin 4 = 218 wide; 82 px
// tall fits 5 lines at the 5.5pt default).
constexpr int kVerseMaxW = 218;
constexpr int kVerseMaxH = 82;
