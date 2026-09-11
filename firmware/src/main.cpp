// ChromaClock — 2.9" quad-colour ePaper clock + weather (Seeed EE05 + XIAO ESP32-S3)
// Inherited eClock rules:
//  - Draw ALL frame content inside the firstPage()/nextPage() paged loop.
//  - Never touch GPIO config on SPI pins shared with the panel.
//  - No manual clear before the first full refresh; init(true) does it.

#include <Arduino.h>
#include <GxEPD2_BW.h>   // quad-colour uses GxEPD2_4C via GxEPD2_4C.h in bring-up
#include "board_pins.h"
#include "chroma_version.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("ChromaClock %s boot\n", CHROMACLOCK_VERSION);
    // Display bring-up lands in Phase 1 (docs/PROJECT_PLAN.md).
}

void loop() {
    delay(1000);
}
