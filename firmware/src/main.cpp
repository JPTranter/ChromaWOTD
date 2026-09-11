// ChromaClock — 2.9" quad-colour ePaper clock + weather
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

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("ChromaClock %s boot\n", CHROMACLOCK_VERSION);

    epaper.begin();
    epaper.fillScreen(TFT_WHITE);
    epaper.update();

    // Bring-up smoke test: one band per colour.
    // 128x296 panel, portrait; draw 4 horizontal bands then rotate usage later.
    for (int y = 0; y < 296; y++) {
        uint32_t c = TFT_WHITE;
        if (y < 74)      c = TFT_BLACK;
        else if (y < 148) c = TFT_RED;
        else if (y < 222) c = TFT_YELLOW;
        for (int x = 0; x < 128; x++) epaper.drawPixel(x, y, c);
    }
    epaper.update();
    Serial.println("4-colour test pattern pushed");
    epaper.sleep();
}

void loop() {
    delay(1000);
}
