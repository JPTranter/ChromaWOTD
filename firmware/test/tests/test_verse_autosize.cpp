// test_verse_autosize — asserts the verse-block font auto-size ladder picks the
// expected body font for real content lengths, including the live 2026-09-13
// Verse of the Day. CMake compiles this target with
// -DCHROMAWOTD_FONT_FREESANS=1 (the device build flag) regardless of the rest
// of the suite, because the ladder only exists under that flag — this is the
// only local coverage of the code path the panel actually runs.
#include <gtest/gtest.h>

#include "verse_display.h"

namespace {

// The layout's verse text block (drawLayout -> drawVerseBlock).
const int kW = kVerseMaxW;   // 218
const int kH = kVerseMaxH;   // 82

// 2026-09-13 BibleGateway VOTD (NIV), after cc_stripLeadingBracket removes the
// "[Final Exhortations] " heading and cc_htmlDecode maps the curly quotes.
const char* kTodaysVerse =
    "\"Rejoice in the Lord always. I will say it again: Rejoice!\"";

// 191 chars: wraps to 4 lines at 6pt (capacity 3) but 4 lines at 5.5pt
// (capacity 4) -> the middle rung of the ladder.
const char* kMediumVerse =
    "The Lord is my shepherd, I lack nothing. He makes me lie down in green "
    "pastures, he leads me beside quiet waters, he refreshes my soul. He guides "
    "me along the right paths for his name's sake.";

// 316 chars: needs more lines than even 5pt capacity allows to be comfortable;
// the ladder must bottom out at the smallest font.
const char* kLongVerse =
    "But the fruit of the Spirit is love, joy, peace, forbearance, kindness, "
    "goodness, faithfulness, gentleness and self-control. Against such things "
    "there is no law. Those who belong to Christ Jesus have crucified the flesh "
    "with its passions and desires. Since we live by the Spirit, let us keep in "
    "step with the Spirit.";

}  // namespace

TEST(VerseAutosize, TodaysVerseSelects6pt) {
    // Short two-line verse: the largest candidate must win.
    EXPECT_EQ(cc_verseFontSize(kTodaysVerse, kW, kH), VerseFontSize::Pt6);
}

TEST(VerseAutosize, MediumVerseSelects55pt) {
    EXPECT_EQ(cc_verseFontSize(kMediumVerse, kW, kH), VerseFontSize::Pt55);
}

TEST(VerseAutosize, LongVerseSelects5pt) {
    EXPECT_EQ(cc_verseFontSize(kLongVerse, kW, kH), VerseFontSize::Pt5);
}

TEST(VerseAutosize, LadderIsMonotonicInLength) {
    // Longer text never selects a LARGER font (enum order: Pt5 < Pt55 < Pt6).
    EXPECT_GE(static_cast<int>(cc_verseFontSize(kTodaysVerse, kW, kH)),
              static_cast<int>(cc_verseFontSize(kMediumVerse, kW, kH)));
    EXPECT_GE(static_cast<int>(cc_verseFontSize(kMediumVerse, kW, kH)),
              static_cast<int>(cc_verseFontSize(kLongVerse, kW, kH)));
}

TEST(VerseAutosize, EmptyVerseSelectsLargest) {
    // No text wraps to zero lines: the largest font trivially fits.
    EXPECT_EQ(cc_verseFontSize("", kW, kH), VerseFontSize::Pt6);
}
