// CHROMAWOTD — 2.9" quad-colour ePaper clock + weather
// Seeed EE05 (XIAO ESP32-S3 Plus) + 2.9" BWRY ePaper (JD79661).
//
// Seeed GFX EPaper API (inherited lessons still apply: colour ePaper updates
// are slow — full refresh ~25 s — so we draw rarely and completely).

#include <Arduino.h>
// Seeed_GFX is a flat-layout Arduino library: its root TFT_eSPI.cpp includes
// Processors + Extensions (EPaper etc.). Include it wholesale so everything
// lands in this TU (lib_build_src_filter in platformio.ini excludes the
// subfolders and TFT_eSPI.cpp from the library's own build).
#include "TFT_eSPI.cpp"
#include "driver.h"
#include "chroma_version.h"

#ifdef EPAPER_ENABLE
EPaper epaper;
#else
#error "EPAPER_ENABLE not set - check BOARD_SCREEN_COMBO in driver.h"
#endif

#include "verse_display.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("CHROMAWOTD %s boot\n", CHROMAWOTD_VERSION);

    epaper.begin();
    epaper.setRotation(1);
    epaper.fillScreen(TFT_WHITE);

    VerseData v = {
        "Fri, Sep 12",
        "Trust in the Lord with all your heart, and do not lean on your own understanding. In all your ways acknowledge him, and he will make straight your paths.",
        "he will make straight your paths",
        "Proverbs 3:5-6"
    };
    WeatherData w = { 27.0f, "Partly cloudy", "Rain likely after 4 PM", 3 };

    drawLayoutLandscape(v, w);
    epaper.update();
    Serial.println("CHROMAWOTD landscape layout pushed to display");
    epaper.sleep();
}

void loop() {
    delay(1000);
}

