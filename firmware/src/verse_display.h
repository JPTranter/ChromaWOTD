#pragma once
#include <cstdint>

// Semantic ePaper colors
#define CC_WHITE 0u
#define CC_BLACK 1u
#define CC_RED 2u
#define CC_YELLOW 3u

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
    // false == there is NO reading (the fetch failed). temp/condition are then
    // meaningless and must NOT be drawn: the old code defaulted to 0.0 and rendered a
    // confident "0°C", which is a fabricated measurement rather than a missing one.
    // Defaults to true so existing 4-element aggregate initialisers (tests, fixtures)
    // keep their previous meaning.
    bool valid = true;
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
// Ordered smallest -> largest: the monotonicity test relies on this ordering, so new
// rungs are APPENDED. The ladder tops out at 10pt: with the verse owning the whole panel,
// short content (a short verse, a brief word definition) can carry a much larger face, and
// the ladder steps down through 9/8/7/6/5.5/5pt as the text needs more room. The reader is
// 55; bigger type is the point of the device.
enum class VerseFontSize { Pt5, Pt55, Pt6, Pt7, Pt8, Pt9, Pt10 };
VerseFontSize cc_verseFontSize(const char* verse, int maxW, int maxH);

// Verse text-block geometry — single source of truth shared by drawLayout()
// and the auto-size tests. The verse owns the whole panel width now that the
// weather column is gone: 296 - 2 * kVerseMargin 4 = 288 wide; 82 px tall holds
// 4 lines at the largest ladder step (8pt, yAdvance 21) and 5 at 6pt.
constexpr int kVerseMaxW = 288;
constexpr int kVerseMaxH = 82;
