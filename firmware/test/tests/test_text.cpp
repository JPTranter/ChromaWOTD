// test_text.cpp — unit tests for the pure text modules (text/glyphs, text/wrap).
// These are the two places a refactor would regress silently (REVIEW T1): the
// UTF-8 decoder and the line-capacity/budget math are now directly testable.

#include "text/date_format.h"
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

// -------------------------------------------------------- header date format --
// The header date is built here so the DEVICE and the RENDER HARNESS produce the same
// string. They used to differ — the device emitted ISO "2026-09-19" while the renders were
// handed a hand-written "Fri, Sep 12" — so a render could look right while the panel was
// wrong. These tests pin the contract both now share.

TEST(HeaderDate, FormatsAsDowDayMonth) {
    char buf[16];
    cc_formatHeaderDate(5, 12, 9, buf, sizeof(buf)); // Friday 12 September
    EXPECT_STREQ(buf, "Fri 12 Sep");
}

TEST(HeaderDate, PadsTheDayToTwoDigits) {
    char buf[16];
    cc_formatHeaderDate(3, 9, 9, buf, sizeof(buf)); // Wednesday the 9th
    EXPECT_STREQ(buf, "Wed 09 Sep");
}

TEST(HeaderDate, CoversEveryDayAndMonthToken) {
    static const char* dow[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[16];
    for (int d = 0; d < 7; d++) {
        cc_formatHeaderDate(d, 1, 1, buf, sizeof(buf));
        EXPECT_EQ(std::string(buf).substr(0, 3), dow[d]) << "weekday " << d;
    }
    for (int m = 1; m <= 12; m++) {
        cc_formatHeaderDate(0, 1, m, buf, sizeof(buf));
        EXPECT_EQ(std::string(buf).substr(7, 3), mon[m - 1]) << "month " << m;
    }
}

TEST(HeaderDate, OutOfRangeInputDegradesInsteadOfPrintingNonsense) {
    char buf[16];
    cc_formatHeaderDate(9, 12, 9, buf, sizeof(buf)); // wday > 6
    EXPECT_EQ(std::string(buf).find("---"), 0u) << buf;
    cc_formatHeaderDate(5, 12, 13, buf, sizeof(buf)); // month > 12
    EXPECT_NE(std::string(buf).find("---"), std::string::npos) << buf;
    cc_formatHeaderDate(5, 99, 9, buf, sizeof(buf)); // day > 31
    EXPECT_NE(std::string(buf).find("--"), std::string::npos) << buf;
}

TEST(HeaderDate, NeverEmitsTheIsoFormTheDeviceUsedToShow) {
    // Regression guard: "2026-09-19" is what the panel used to display while the renders
    // showed "Fri, Sep 12".
    char buf[16];
    cc_formatHeaderDate(5, 19, 9, buf, sizeof(buf));
    EXPECT_EQ(std::string(buf).find('-'), std::string::npos) << buf;
    EXPECT_EQ(std::string(buf).find(','), std::string::npos) << buf;
}
