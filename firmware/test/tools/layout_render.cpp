// layout_render — render a verse/weather fixture through the real (single, light,
// landscape) layout engine and write a PNG, without touching hardware.
//
//   ./layout_render --verse-file verse.txt --highlight "he will make straight your paths" \
//                   --reference "Proverbs 3:5-6" --temp -2.5 --condition "Partly cloudy" \
//
// Pass "-" as --verse-file to read the verse from stdin. Every draw path is the
// same code the device runs (only the backend differs), so what you see here is
// what the panel will refresh to.
#include "../harness/canvas.h"
#include "net/net.h"
#include "text/date_format.h"
#include "verse_display.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

const char* verseFontName(VerseFontSize s) {
    switch (s) {
    case VerseFontSize::Pt5:
        return "Pt5";
    case VerseFontSize::Pt55:
        return "Pt55";
    case VerseFontSize::Pt7:
        return "7pt";
    case VerseFontSize::Pt8:
        return "8pt";
    case VerseFontSize::Pt9:
        return "9pt";
    case VerseFontSize::Pt10:
        return "10pt";
    case VerseFontSize::Pt6:
        return "Pt6";
    }
    return "Pt55";
}

const char* kUsage = "usage: layout_render [options]\n"
                     "\n"
                     "  --verse \"text\"                     verse body text\n"
                     "  --verse-file <path>                read verse body from a file ('-' for stdin)\n"
                     "  --highlight \"phrase\"               phrase painted in red (optional)\n"
                     "  --reference \"Proverbs 3:5-6\"       citation painted in red (optional)\n"
                     "  --temp <celsius>                   temperature, floats and negatives allowed\n"
                     "  --condition \"Partly cloudy\"        condition label (optional)\n"
                     "  --alert \"Rain after 4 PM\"          alert banner text, drawn in red (optional)\n"
                     "  --date \"Fri 12 Sep\"              header date, explicit string (optional)\n"
                     "  --date-ymd 2026-09-12            header date from a real date, via the SHARED formatter\n"
                     "  --label \"TOMORROW\"                footer marker before the temperature (default FORECAST = none)\n"
                     "  --header \"Verse of the Day\"        header fallback when there is no citation (optional)\n"
                     "  --leftcap \"(bre-VIL-uh-kwuhnt)\"    respelling, drawn beside the word in the header\n"
                     "  --out <path.png>                   output file (default preview.png)\n"
                     "  --help\n"
                     "  --live                         fetch weather + verse from the live network\n"
                     "  --word-live                    fetch Word of the Day + weather and render it\n"
                     "  --word-file <path.html>        render a SAVED A.Word.A.Day page (same parser + body\n"
                     "                                 composer as the device; no network)\n"
                     "  --tomorrow                     with --live/--word-live: use tomorrow's forecast\n"
                     "                                 (curl hook) instead of fixtures; ignore others\n";

struct Args {
    std::string verse;
    std::string verseFile;
    std::string highlight;
    std::string reference;
    std::string condition;
    std::string alert;
    std::string date;
    std::string label = "FORECAST";          // weather column caption
    std::string header = "Verse of the Day"; // yellow-band title
    std::string leftcap;                     // black caption at the left of the bottom rule
    std::string out = "preview.png";
    std::string wordFile; // a saved A.Word.A.Day page to render (--word-file)
    float temp = 21.0f;
    bool haveHighlight = false, haveReference = false, haveAlert = false, haveCondition = false, haveDate = false;
    bool live = false;
    bool wordLive = false;
    bool tomorrow = false;
    bool noWeather = false;
};


std::string readAll(std::istream& in) {
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    for (char& c : text)
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' '; // one paragraph
    return text;
}

} // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "layout_render: " << what << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        a.live = (flag == "--live") || a.live;

        if (flag == "--help" || flag == "-h") {
            std::cout << kUsage;
            return 0;
        } else if (flag == "--live") { /* handled above */
        } else if (flag == "--word-live") {
            a.wordLive = true;
        } else if (flag == "--word-file") {
            // Render a saved A.Word.A.Day page (e.g. the day a problem was reported).
            a.wordFile = next("--word-file");
        } else if (flag == "--tomorrow") {
            a.tomorrow = true;
        } else if (flag == "--verse") {
            a.verse = next("--verse");
        } else if (flag == "--verse-file") {
            a.verseFile = next("--verse-file");
        } else if (flag == "--highlight") {
            a.highlight = next("--highlight");
            a.haveHighlight = true;
        } else if (flag == "--reference") {
            a.reference = next("--reference");
            a.haveReference = true;
        } else if (flag == "--condition") {
            a.condition = next("--condition");
            a.haveCondition = true;
        } else if (flag == "--alert") {
            a.alert = next("--alert");
            a.haveAlert = true;
        } else if (flag == "--date") {
            a.date = next("--date"); // explicit string (tests, odd cases)
            a.haveDate = true;
        } else if (flag == "--date-ymd") {
            // Preferred: format from a real date through the SAME helper the device uses, so
            // a preview cannot show a date format the panel will never produce.
            const std::string ymd = next("--date-ymd");
            static char dateBuf[16];
            int y = 0, m = 0, d = 0;
            if (sscanf(ymd.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
                // Sakamoto's day-of-week (0 = Sunday): deterministic, no <ctime> locale
                // dependence in the harness either.
                static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
                int yy = (m < 3) ? y - 1 : y;
                const int wd = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
                cc_formatHeaderDate(wd, d, m, dateBuf, sizeof(dateBuf));
            } else {
                snprintf(dateBuf, sizeof(dateBuf), "%s", ymd.c_str());
            }
            a.date = dateBuf;
            a.haveDate = true;
        } else if (flag == "--label") {
            a.label = next("--label");
        } else if (flag == "--header") {
            a.header = next("--header");
        } else if (flag == "--leftcap") {
            a.leftcap = next("--leftcap");
        } else if (flag == "--temp") {
            a.temp = std::strtof(next("--temp").c_str(), nullptr);
        }
        // --no-weather: render the "no reading" state (the fetch failed). Without this
        // the preview tool could only produce a VALID reading, which is why the old
        // fabricated "0°C" failure state was never visible during development.
        else if (flag == "--no-weather") {
            a.noWeather = true;
        } else if (flag == "--out") {
            a.out = next("--out");
        } else {
            std::cerr << "layout_render: unknown option '" << flag << "'\n\n" << kUsage;
            return 2;
        }
    }

    if (!a.verseFile.empty()) {
        if (a.verseFile == "-") {
            a.verse = readAll(std::cin);
        } else {
            std::ifstream f(a.verseFile);
            if (!f) {
                std::cerr << "layout_render: cannot open " << a.verseFile << "\n";
                return 1;
            }
            a.verse = readAll(f);
        }
    }
    if (!a.live && !a.wordLive && a.wordFile.empty() && a.verse.empty()) {
        std::cerr << "layout_render: no verse text (use --verse or --verse-file)\n\n" << kUsage;
        return 2;
    }

    VerseData v{a.haveDate ? a.date.c_str() : nullptr, a.verse.c_str(), a.haveHighlight ? a.highlight.c_str() : nullptr,
                a.haveReference ? a.reference.c_str() : nullptr};
    WeatherData w{a.temp, a.haveCondition ? a.condition.c_str() : nullptr, a.haveAlert ? a.alert.c_str() : nullptr};
    if (a.noWeather) {
        w.valid = false;
        w.condition = nullptr;
    }

    g_canvas.init(296, 128);
    LayoutOptions opts;
    opts.headerTitle = a.header.c_str();
    opts.weatherLabel = a.label.c_str();
    opts.leftCaption = a.leftcap.empty() ? nullptr : a.leftcap.c_str();

    // --live: pull the real weather + verse through the shared network module
    // (curl hook on host), exactly as the device will.
    if (a.live) {
        WeatherData lw{};
        VerseData lv{};
        bool wok = cc_fetchWeather(&lw, a.tomorrow), vok = cc_fetchVerse(&lv);
        if (wok) {
            w = lw;
        }
        if (vok) {
            v.verse = static_cast<const char*>(lv.verse);
            v.reference = static_cast<const char*>(lv.reference);
            v.date = lv.date ? static_cast<const char*>(lv.date) : (a.haveDate ? a.date.c_str() : nullptr);
            v.highlight = nullptr;
        }
        printf("live: weather%s verse%s\n", wok ? " OK" : " FAIL(fallback)", vok ? " OK" : " FAIL(fallback)");
    }

    static char wordBody[WORD_BODY_MAX];
    if (a.wordLive || !a.wordFile.empty()) {
        WordData wd{};
        std::string html;
        bool dok = false;
        if (!a.wordFile.empty()) {
            // Render a SPECIFIC A.Word.A.Day page (e.g. a saved copy of a past day's, or the
            // exact page the panel reported a problem with) through the same parser + body
            // composer the device uses.
            std::ifstream f(a.wordFile);
            if (!f) {
                std::cerr << "layout_render: cannot open " << a.wordFile << "\n";
                return 1;
            }
            html.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            dok = cc_parseAwad(html.c_str(), &wd) && wd.definition;
        } else {
            bool wok = cc_fetchWeather(&w, a.tomorrow);
            printf("word-live: weather=%s\n", wok ? "OK" : "FAIL");
            dok = cc_fetchWord(&wd) && wd.definition;
        }
        if (dok) {
            cc_composeWordBody(wd, wordBody, sizeof(wordBody));
            v.verse = wordBody;
            v.reference = wd.word;
            opts.headerTitle = "Word of the Day";
            opts.leftCaption = wd.pronunciation;
        }
        printf("word: %s (%s) edition=%s body=%d chars src=%s\n", dok ? "OK" : "FAIL", dok ? wd.word : "-",
               dok && wd.editionDate ? wd.editionDate : "?", dok ? (int)strlen(wordBody) : 0,
               a.wordFile.empty() ? "live" : a.wordFile.c_str());
    }

    drawLayout(v, w, opts);

    if (!g_canvas.dumpPng(a.out.c_str())) {
        std::cerr << "layout_render: failed to write " << a.out << "\n";
        return 1;
    }

    printf("rendered 296x128 landscape/light -> %s\n", a.out.c_str());
    // Report the auto-size decision the layout just made, straight from the real
    // selector — this is what tools/font_size_probe.py reads, so "which font did
    // today's text get?" is answered by the engine rather than by re-deriving it.
    printf("verse_font=%s\n", verseFontName(cc_verseFontSize(v.verse, kVerseMaxW, kVerseMaxH)));
    printf("temp %.1f C | highlight %s\n", a.temp,
           !a.haveHighlight ? "n/a" : (verseHighlightFound(v) ? "matched" : "NOT FOUND - no red accent"));
    return 0;
}
