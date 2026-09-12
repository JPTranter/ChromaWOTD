// test_net.cpp — network parse + WMO mapping + HTML entity decoding tests.
//
// Covers the SHARED pure functions (cc_wmoCondition, cc_alertFromWmo,
// cc_htmlDecode) that both host and device run, plus — when networking is
// available — the real cc_fetchWeather / cc_fetchVerse against the live
// endpoints through the curl hook. The live-fetch tests are skipped if curl
// isn't present / the network is down, so the suite stays green offline.
#include "net/net.h"
#include "verse_display.h"
#include <cstring>
#include <cstdio>
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// WMO mapping (pure, always run) --------------------------------------------
// ---------------------------------------------------------------------------
TEST(Wmo, Clear_Is_Sun_NoAlert) {
    char b[16]; bool alert = true;
    EXPECT_EQ(cc_wmoCondition(0, b, sizeof(b), &alert), (int)strlen("Clear"));
    EXPECT_STREQ(b, "Clear");
    EXPECT_FALSE(alert);
}

TEST(Wmo, PartlyCloudy_1_2_3) {
    for (int c : {1, 2, 3}) {
        char b[16]; bool alert = true;
        EXPECT_EQ(cc_wmoCondition(c, b, sizeof(b), &alert), (int)strlen("Partly cloudy"));
        EXPECT_STREQ(b, "Partly cloudy");
        EXPECT_FALSE(alert);
    }
}

TEST(Wmo, RainCodes_Give_Rain_NoAlert) {
    for (int c : {61, 63, 65, 66, 67, 80, 81, 82}) {
        char b[16]; bool alert = true;
        EXPECT_EQ(cc_wmoCondition(c, b, sizeof(b), &alert), (int)strlen("Rain"));
        EXPECT_STREQ(b, "Rain");
        EXPECT_FALSE(alert);
    }
}

TEST(Wmo, Thunderstorm_Sets_Alert) {
    char b[16]; bool alert = false;
    EXPECT_EQ(cc_wmoCondition(95, b, sizeof(b), &alert), (int)strlen("Thunderstorm"));
    EXPECT_STREQ(b, "Thunderstorm");
    EXPECT_TRUE(alert);
}

TEST(Wmo, Missing_Alert_Returns_Empty_String) {
    char b[8];
    EXPECT_EQ(cc_alertFromWmo(0, b, sizeof(b)), 0);
    EXPECT_STREQ(b, "");
}

TEST(Wmo, Thunderstorm_Alert_Text) {
    char b[64];
    EXPECT_GT(cc_alertFromWmo(95, b, sizeof(b)), 0);
    EXPECT_STREQ(b, "Severe weather warning");
}

// ---------------------------------------------------------------------------
// HTML entity decoding (pure, always run) -----------------------------------
// ---------------------------------------------------------------------------
TEST(HtmlDecode, CurlyAndEntityQuotes_Become_AsciiQuotes) {
    char s[] = "&ldquo;Rejoice&rdquo; &amp; &mdash; &ndash;";
    cc_htmlDecode(s);
    EXPECT_STREQ(s, "\"Rejoice\" & - -");
}

TEST(HtmlDecode, NumericEntities) {
    char s[] = "&#8220;hi&#8221; &#8212; &#160;x";
    cc_htmlDecode(s);
    EXPECT_STREQ(s, "\"hi\" -  x");
}

TEST(HtmlDecode, UnknownEntity_Keeps_Text) {
    char s[] = "a &bogus; z";
    cc_htmlDecode(s);
    EXPECT_STREQ(s, "a &bogus; z");
}

// ---------------------------------------------------------------------------
// Section-heading strip (pure, always run) ----------------------------------
// ---------------------------------------------------------------------------
TEST(StripBracket, QuotedHeading_Removed_Quotes_Kept) {
    char s[] = "\"[Final Exhortations]  Rejoice in the Lord always.\"";
    cc_stripLeadingBracket(s);
    EXPECT_STREQ(s, "\"Rejoice in the Lord always.\"");
}

TEST(StripBracket, BareHeading_Removed) {
    char s[] = "[Heading] Body text here";
    cc_stripLeadingBracket(s);
    EXPECT_STREQ(s, "Body text here");
}

TEST(StripBracket, NoHeading_Unchanged_Quotes_Kept) {
    char s[] = "\"Rejoice in the Lord always.\"";
    cc_stripLeadingBracket(s);
    EXPECT_STREQ(s, "\"Rejoice in the Lord always.\"");
}

TEST(StripBracket, UnclosedBracket_Unchanged) {
    char s[] = "\"[unclosed heading and no close";
    cc_stripLeadingBracket(s);
    EXPECT_STREQ(s, "\"[unclosed heading and no close");
}

// ---------------------------------------------------------------------------
// Live fetch (skipped when offline) -----------------------------------------
// ---------------------------------------------------------------------------
TEST(Fetch, Weather_Live) {
    WeatherData w;
    if (!cc_fetchWeather(&w)) { GTEST_SKIP() << "network unavailable (no curl)"; }
    // Sanity: a real temperature and a condition string from the WMO map; alert
    // may be null (no severe weather right now).
    EXPECT_GT(w.temp, -50.0f);
    EXPECT_LT(w.temp, 60.0f);
    EXPECT_TRUE(w.condition && w.condition[0]);
}

TEST(Fetch, Verse_Live) {
    VerseData v;
    if (!cc_fetchVerse(&v)) { GTEST_SKIP() << "network unavailable (no curl)"; }
    EXPECT_TRUE(v.verse && v.verse[0]);
    EXPECT_TRUE(v.reference && v.reference[0]);
    EXPECT_TRUE(v.date && v.date[0]);
    // The rendered verse must be ASCII-safe (the renderer expects it).
    for (const unsigned char* p = (const unsigned char*)v.verse; *p; ++p)
        EXPECT_LT(*p, 0x80) << "non-ASCII byte 0x" << std::hex << (int)*p << " reached the renderer";
    EXPECT_TRUE(v.highlight == nullptr);  // not set this phase
}