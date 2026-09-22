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
// The verse owns the whole panel now (no weather column), so the block is wider than it was,
// and the ladder above it tops out at 10pt — the 9/10pt rungs exist so short content (a brief
// verse, a Word-of-the-Day definition) can use the space it was leaving empty.
const int kW = kVerseMaxW;   // 288
const int kH = kVerseMaxH;   // 93 (startY 22 to the footer row at 115)

// 2026-09-13 BibleGateway VOTD (NIV), after cc_stripLeadingBracket removes the
// "[Final Exhortations] " heading and cc_htmlDecode maps the curly quotes.
const char* kTodaysVerse =
    "\"Rejoice in the Lord always. I will say it again: Rejoice!\"";

// 191 chars: too long for the 8pt rung, fits at 7pt -> a middle rung, not an end one.
const char* kMediumVerse =
    "The Lord is my shepherd, I lack nothing. He makes me lie down in green "
    "pastures, he leads me beside quiet waters, he refreshes my soul. He guides "
    "me along the right paths for his name's sake.";

// 316 chars: the longest realistic content. The ladder must step all the way down —
// and note it now lands on 5.5pt rather than 5pt, because the wider block buys a rung
// even for the worst case.
const char* kLongVerse =
    "But the fruit of the Spirit is love, joy, peace, forbearance, kindness, "
    "goodness, faithfulness, gentleness and self-control. Against such things "
    "there is no law. Those who belong to Christ Jesus have crucified the flesh "
    "with its passions and desires. Since we live by the Spirit, let us keep in "
    "step with the Spirit.";

}  // namespace

TEST(VerseAutosize, ShortVerseSelectsTheLargestRung) {
    // A short verse gets the biggest face the ladder offers — the whole point of giving the
    // text the full panel width, and of raising the ceiling to 10pt for a reader who needs
    // larger type.
    EXPECT_EQ(cc_verseFontSize(kTodaysVerse, kW, kH), VerseFontSize::Pt10);
}

TEST(VerseAutosize, WordDefinitionGetsTheLargestRung) {
    // The case that motivated the 9/10pt rungs: a Word-of-the-Day definition is typically
    // short, and at 8pt it left the lower third of the panel empty.
    const char* definition =
        "Decedent: a dead person. The decedent's estate was settled by the court.";
    EXPECT_EQ(cc_verseFontSize(definition, kW, kH), VerseFontSize::Pt10);
}

TEST(VerseAutosize, MediumVerseSelects7pt) {
    EXPECT_EQ(cc_verseFontSize(kMediumVerse, kW, kH), VerseFontSize::Pt7);
}

TEST(VerseAutosize, LongVerseStepsDownTo55pt) {
    // Previously 5pt; the wider block buys a rung even for the longest realistic verse.
    EXPECT_EQ(cc_verseFontSize(kLongVerse, kW, kH), VerseFontSize::Pt55);
}

TEST(VerseAutosize, LongestVerseIsStillReadableAtWorst) {
    // The ladder must never bottom out below 5.5pt for realistic content: a floor of 5pt
    // on this panel would be smaller than the 6pt the device shipped with before, which
    // would defeat the exercise.
    EXPECT_GT(static_cast<int>(cc_verseFontSize(kLongVerse, kW, kH)),
              static_cast<int>(VerseFontSize::Pt5));
}

TEST(VerseAutosize, LadderIsMonotonicInLength) {
    // Longer text never selects a LARGER font (enum order: Pt5 < Pt55 < Pt6 < Pt7 < Pt8 <
    // Pt9 < Pt10).
    EXPECT_GE(static_cast<int>(cc_verseFontSize(kTodaysVerse, kW, kH)),
              static_cast<int>(cc_verseFontSize(kMediumVerse, kW, kH)));
    EXPECT_GE(static_cast<int>(cc_verseFontSize(kMediumVerse, kW, kH)),
              static_cast<int>(cc_verseFontSize(kLongVerse, kW, kH)));
}

TEST(VerseAutosize, EmptyVerseSelectsLargest) {
    // No text wraps to zero lines: the largest font trivially fits.
    EXPECT_EQ(cc_verseFontSize("", kW, kH), VerseFontSize::Pt10);
}
