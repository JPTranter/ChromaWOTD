// layout_render — render a verse/weather fixture through the real (single, light,
// landscape) layout engine and write a PNG, without touching hardware.
//
//   ./layout_render --verse-file verse.txt --highlight "he will make straight your paths" \
//                   --reference "Proverbs 3:5-6" --temp -2.5 --condition "Partly cloudy" \
//                   --icon partly --alert "Rain likely after 4 PM" --out preview.png
//
// Pass "-" as --verse-file to read the verse from stdin. Every draw path is the
// same code the device runs (only the backend differs), so what you see here is
// what the panel will refresh to.
#include "../harness/canvas.h"
#include "verse_display.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

const char* kUsage =
    "usage: layout_render [options]\n"
    "\n"
    "  --verse \"text\"                     verse body text\n"
    "  --verse-file <path>                read verse body from a file ('-' for stdin)\n"
    "  --highlight \"phrase\"               phrase painted in red (optional)\n"
    "  --reference \"Proverbs 3:5-6\"       citation painted in red (optional)\n"
    "  --temp <celsius>                   temperature, floats and negatives allowed\n"
    "  --condition \"Partly cloudy\"        condition label (optional)\n"
    "  --alert \"Rain after 4 PM\"          alert banner text, drawn in red (optional)\n"
    "  --icon sun|cloud|rain|partly|0..3  weather icon (default partly)\n"
    "  --date \"Fri, Sep 12\"              header date (optional)\n"
    "  --out <path.png>                   output file (default preview.png)\n"
    "  --help\n";

struct Args {
    std::string verse;
    std::string verseFile;
    std::string highlight;
    std::string reference;
    std::string condition;
    std::string alert;
    std::string date;
    std::string out = "preview.png";
    float temp = 21.0f;
    int icon = 3;
    bool haveHighlight = false, haveReference = false, haveAlert = false,
         haveCondition = false, haveDate = false;
};

int iconFromName(const std::string& name) {
    if (name == "sun")    return 0;
    if (name == "cloud")  return 1;
    if (name == "rain")   return 2;
    if (name == "partly") return 3;
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '3') return name[0] - '0';
    return -1;
}

std::string readAll(std::istream& in) {
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    for (char& c : text) if (c == '\n' || c == '\r' || c == '\t') c = ' ';  // one paragraph
    return text;
}

}  // namespace

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

        if (flag == "--help" || flag == "-h") { std::cout << kUsage; return 0; }
        else if (flag == "--verse")       { a.verse = next("--verse"); }
        else if (flag == "--verse-file")  { a.verseFile = next("--verse-file"); }
        else if (flag == "--highlight")   { a.highlight = next("--highlight"); a.haveHighlight = true; }
        else if (flag == "--reference")   { a.reference = next("--reference"); a.haveReference = true; }
        else if (flag == "--condition")   { a.condition = next("--condition"); a.haveCondition = true; }
        else if (flag == "--alert")       { a.alert = next("--alert"); a.haveAlert = true; }
        else if (flag == "--date")        { a.date = next("--date"); a.haveDate = true; }
        else if (flag == "--temp")        { a.temp = std::strtof(next("--temp").c_str(), nullptr); }
        else if (flag == "--out")         { a.out = next("--out"); }
        else if (flag == "--icon") {
            int icon = iconFromName(next("--icon"));
            if (icon < 0) { std::cerr << "layout_render: --icon expects sun|cloud|rain|partly|0..3\n"; return 2; }
            a.icon = icon;
        }
        else {
            std::cerr << "layout_render: unknown option '" << flag << "'\n\n" << kUsage;
            return 2;
        }
    }

    if (!a.verseFile.empty()) {
        if (a.verseFile == "-") {
            a.verse = readAll(std::cin);
        } else {
            std::ifstream f(a.verseFile);
            if (!f) { std::cerr << "layout_render: cannot open " << a.verseFile << "\n"; return 1; }
            a.verse = readAll(f);
        }
    }
    if (a.verse.empty()) {
        std::cerr << "layout_render: no verse text (use --verse or --verse-file)\n\n" << kUsage;
        return 2;
    }

    VerseData v{ a.haveDate ? a.date.c_str() : nullptr,
                 a.verse.c_str(),
                 a.haveHighlight ? a.highlight.c_str() : nullptr,
                 a.haveReference ? a.reference.c_str() : nullptr };
    WeatherData w{ a.temp,
                   a.haveCondition ? a.condition.c_str() : nullptr,
                   a.haveAlert ? a.alert.c_str() : nullptr,
                   static_cast<WeatherIcon>(a.icon) };

    g_canvas.init(296, 128);
    drawLayout(v, w);

    if (!g_canvas.dumpPng(a.out.c_str())) {
        std::cerr << "layout_render: failed to write " << a.out << "\n";
        return 1;
    }

    printf("rendered 296x128 landscape/light -> %s\n", a.out.c_str());
    printf("temp %.1f C | icon %d | highlight %s\n", a.temp, a.icon,
           !a.haveHighlight ? "n/a" : (verseHighlightFound(v) ? "matched" : "NOT FOUND - no red accent"));
    return 0;
}