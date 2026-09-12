// test_text.cpp — unit tests for the pure text modules (text/glyphs, text/wrap).
// These are the two places a refactor would regress silently (REVIEW T1): the
// UTF-8 decoder and the line-capacity/budget math are now directly testable.

#include "text/glyphs.h"
#include "text/wrap.h"
#include <gtest/gtest.h>

// ----------------------------------------------------------- cc_lineCapacity --

TEST(LineCapacity, ZeroAndNegativeHeightsYieldNoLines) {
    EXPECT_EQ(cc_lineCapacity(0, 1, 10), 0);
    EXPECT_EQ(cc_lineCapacity(-5, 1, 10), 0);
}

TEST(LineCapacity, TinyHeightsBelowGlyphYieldNoLines) {
    // maxH < 8*size -> no line fits (the 8px inset is the glyph's own footprint).
    EXPECT_EQ(cc_lineCapacity(7, 1, 10), 0);
    EXPECT_EQ(cc_lineCapacity(8, 1, 10), 1);   // exactly the threshold
}

TEST(LineCapacity, NormalHeightsCountLines) {
    // (maxH - 8*size) / lineHeight + 1
    EXPECT_EQ(cc_lineCapacity(18, 1, 10), 2);  // (18-8)/10 + 1 = 2
    EXPECT_EQ(cc_lineCapacity(28, 1, 10), 3);  // (28-8)/10 + 1 = 3
    EXPECT_EQ(cc_lineCapacity(82, 1, 12), 7);  // (82-8)/12 + 1 = 7 (verse block)
}

TEST(LineCapacity, TinyLineHeightDoesNotDivideByZero) {
    // lineHeight is always >= 1 in practice (yAdvance), but the function must not
    // divide by zero if handed 0.
    EXPECT_GE(cc_lineCapacity(100, 1, 1), 1);
}

// ------------------------------------------------------------- cc_lineBudget --

TEST(LineBudget, NoTruncationReturnsFullWidth) {
    EXPECT_EQ(cc_lineBudget(100, 6, false, 0, 5), 100);
}

TEST(LineBudget, TruncationReservesSlackOnlyOnLastLine) {
    // slack = 6 glyph widths; only the final line (lineIdx == capacity-1) shrinks.
    EXPECT_EQ(cc_lineBudget(100, 6, true, 0, 5), 100);          // not the last line
    EXPECT_EQ(cc_lineBudget(100, 6, true, 4, 5), 100 - 6 * 6);  // last line: 64
}

TEST(LineBudget, NarrowColumnDegradesToFullWidth) {
    // When budget < 4*charWidth the reservation turns off (marker degrades to "."),
    // returning maxW rather than a sub-4-glyph budget.
    EXPECT_EQ(cc_lineBudget(20, 6, true, 4, 5), 20);  // 20 - 36 < 0 -> maxW
}

// ------------------------------------------------------------ cc_utf8ToAscii --

static unsigned char decodeOne(const unsigned char* p) {
    unsigned char out = 0;
    cc_utf8ToAscii(p, &out);
    return out;
}

TEST(Utf8Decoder, AsciiPassesThrough) {
    EXPECT_EQ(decodeOne((const unsigned char*)"A"), 'A');
    EXPECT_EQ(decodeOne((const unsigned char*)" "), ' ');
    EXPECT_EQ(decodeOne((const unsigned char*)"0"), '0');
}

TEST(Utf8Decoder, TypographicGlyphsMapToAscii) {
    // degree -> 0xB0 sentinel
    EXPECT_EQ(decodeOne((const unsigned char*)"\xC2\xB0"), (unsigned char)0xB0);
    // non-breaking space -> ' '
    EXPECT_EQ(decodeOne((const unsigned char*)"\xC2\xA0"), ' ');
    // em-dash -> '-'
    EXPECT_EQ(decodeOne((const unsigned char*)"\xE2\x80\x94"), '-');
    // curly quotes -> '"' / '\''
    EXPECT_EQ(decodeOne((const unsigned char*)"\xE2\x80\x9C"), '"');
    // ellipsis -> '.'
    EXPECT_EQ(decodeOne((const unsigned char*)"\xE2\x80\xA6"), '.');
}

// ------------------------------------------------------- cc_utf8ToAsciiN (S2) --

TEST(Utf8DecoderBounded, TruncatedLeadByteCollapsesToOneQuestion) {
    // A 3-byte lead (0xE2) followed by only one continuation byte must collapse to
    // one '?' and consume only the lead byte — no read past the bound.
    const unsigned char seq[] = { 0xE2, 0x80, 'x' };
    unsigned char out = 0;
    int n = cc_utf8ToAsciiN(seq, seq + 2, &out);   // bound ends mid-grapheme
    EXPECT_EQ(out, '?');
    EXPECT_EQ(n, 1);
}

TEST(Utf8DecoderBounded, LoneFourByteLeadCollapsesToOneQuestion) {
    const unsigned char seq[] = { 0xF0, 'x' };
    unsigned char out = 0;
    int n = cc_utf8ToAsciiN(seq, seq + 1, &out);
    EXPECT_EQ(out, '?');
    EXPECT_EQ(n, 1);
}
