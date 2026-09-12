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
TEST(Fetch, Weather_Live_Today) {
    WeatherData w;
    if (!cc_fetchWeather(&w, false)) { GTEST_SKIP() << "network unavailable (no curl)"; }
    // Sanity: a real temperature and a condition string from the WMO map; alert
    // may be null (no severe weather right now).
    EXPECT_GT(w.temp, -50.0f);
    EXPECT_LT(w.temp, 60.0f);
    EXPECT_TRUE(w.condition && w.condition[0]);
}

TEST(Fetch, Weather_Live_Tomorrow) {
    WeatherData w;
    if (!cc_fetchWeather(&w, true)) { GTEST_SKIP() << "network unavailable (no curl)"; }
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

// ---------------------------------------------------------------------------
// Word of the Day (A.Word.A.Day parser) -------------------------------------
// ---------------------------------------------------------------------------
// A trimmed but structurally faithful slice of wordsmith.org/words/today.html:
// each section is <div style="...">LABEL:</div>\n<div ...>\nVALUE\n</div><br>.
static const char kAwadPage[] = R"HTML(<html><head>
<TITLE>A.Word.A.Day --breviloquent</TITLE>
<meta property="og:title" content="breviloquent" />
</head><body>
<h3>
breviloquent
</h3>
<p>

<div style="font-family:Verdana; color:#555555; font-size:13px;">PRONUNCIATION:</div>
<div style="margin-left: 20px;">
(bre-VIL-uh-kwuhnt)
<a href="https://wordsmith.org/words/breviloquent.mp3"><img src="x.png"></a>
</div><br>

<div style="font-family:Verdana; color:#555555; font-size:13px;">MEANING:</div>
<div style="margin-left: 20px;">
<i>adjective</i>: Using few words.
</div><br>

<div style="font-family:Verdana; color:#555555; font-size:13px;">ETYMOLOGY:</div>
<div style="margin-left: 20px;">
 From Latin brevis (short) + loqui (to speak).
</div><br>

<div style="font-family:Verdana; color:#555555; font-size:13px;">USAGE:</div>
<div style="margin-left: 20px;">
&#8220;[The driver] raised his hands from the wheel.&#8221;<br>
Tara Gallagher; Town &amp; Country; Nov 2012.
</div><br>

</body></html>)HTML";

TEST(AwadParse, ExtractsAllFields) {
    WordData w{};
    ASSERT_TRUE(cc_parseAwad(kAwadPage, &w));
    EXPECT_STREQ(w.word, "breviloquent");
    EXPECT_STREQ(w.pronunciation, "(bre-VIL-uh-kwuhnt)");
    EXPECT_STREQ(w.definition, "adjective: Using few words.");
}

TEST(AwadParse, ExampleIsTheQuotedSentence_AttributionDropped) {
    WordData w{};
    ASSERT_TRUE(cc_parseAwad(kAwadPage, &w));
    ASSERT_NE(w.example, nullptr);
    EXPECT_STREQ(w.example, "\"[The driver] raised his hands from the wheel.\"");
    // The attribution that follows the closing quote must not leak in.
    EXPECT_EQ(strstr(w.example, "Tara Gallagher"), nullptr);
}

TEST(AwadParse, RenderedFieldsArePrintableAscii) {
    WordData w{};
    ASSERT_TRUE(cc_parseAwad(kAwadPage, &w));
    // Not merely < 0x80: control characters (newlines from the source HTML)
    // would pass that check yet render as garbage on the panel.
    for (const char* f : {w.word, w.pronunciation, w.definition, w.example}) {
        ASSERT_NE(f, nullptr);
        for (const unsigned char* p = (const unsigned char*)f; *p; ++p)
            EXPECT_GE(*p, 0x20) << "control char 0x" << std::hex << (int)*p << " in '" << f << "'";
        for (const unsigned char* p = (const unsigned char*)f; *p; ++p)
            EXPECT_LE(*p, 0x7E) << "non-ASCII byte 0x" << std::hex << (int)*p << " in '" << f << "'";
    }
}

TEST(AwadParse, NewlinesInSourceAreCollapsedToSpaces) {
    // The real page wraps long sections across lines; the value must come out as
    // one normalised line, not with embedded newlines.
    static const char kPage[] = R"HTML(
<h3>
wordy
</h3>
<div style="x">MEANING:</div>
<div style="margin-left: 20px;">
adjective: Spread
across
lines.
</div><br>
)HTML";
    WordData w{};
    ASSERT_TRUE(cc_parseAwad(kPage, &w));
    EXPECT_STREQ(w.definition, "adjective: Spread across lines.");
    EXPECT_EQ(strchr(w.definition, '\n'), nullptr);
}

TEST(AwadParse, MalformedPageFails) {
    WordData w{};
    EXPECT_FALSE(cc_parseAwad("<html><body>no sections here</body></html>", &w));
}

TEST(Fetch, Word_Live) {
    WordData w{};
    if (!cc_fetchWord(&w)) { GTEST_SKIP() << "network unavailable (no curl) / page shape changed"; }
    EXPECT_TRUE(w.word && w.word[0]);
    EXPECT_TRUE(w.definition && w.definition[0]);
}