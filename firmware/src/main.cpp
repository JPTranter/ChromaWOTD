// CHROMAWOTD — 2.9" quad-colour ePaper Verse/Word of the Day + weather display.
// Seeed EE05 (XIAO ESP32-S3 Plus) + 2.9" BWRY ePaper (JD79661 panel / JD79667 driver IC).
//
// Phase 3 main: connect Wi-Fi, sync NTP, fetch weather (Open-Meteo) + verse
// (BibleGateway) over validated TLS, then run the single landscape layout and
// do one full ~25s refresh. Falls back to an inline verse / plain weather and
// an OFFLINE banner when the network is unavailable. See docs/ARCHITECTURE.md
// for the full state machine (Boot → Sync → Render → Sleep → wake).
//
// STACK NOTE: mbedtls's entropy gathering + CTR-DRBG reseed during the first
// TLS handshake needs more stack than the default Arduino loopTask (8 KB on
// this core) provides — it overflows partway through start_ssl_client(). This
// project ships the precompiled Arduino-ESP32 core (not an IDF component
// build), so framework sdkconfig.h already `#define`s CONFIG_MAIN_TASK_STACK_SIZE
// and a build_flags override is silently shadowed — bumping it is not
// available without rebuilding the framework. The portable fix that works with
// the precompiled core: run the whole Sync phase (Wi-Fi + TLS fetch) on a
// dedicated FreeRTOS task created with an explicit 16 KB stack, and have
// setup() block on a semaphore until it finishes.

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
// Seeed_GFX is a flat-layout Arduino library: its root TFT_eSPI.cpp includes
// Processors + Extensions (EPaper etc.) itself. Include it wholesale so everything
// lands in this TU; PlatformIO only compiles the library's root directory, so no
// other Seeed_GFX translation unit competes with it.
#include "TFT_eSPI.cpp"
#include "chroma_version.h"

#ifdef EPAPER_ENABLE
EPaper epaper;
#else
#error "EPAPER_ENABLE not set - check BOARD_SCREEN_COMBO / USE_XIAO_EPAPER_DISPLAY_BOARD_EE05 build_flags in platformio.ini"
#endif

#include "verse_display.h"
#include "net/net.h"

#ifdef CHROMAWOTD_NETWORK
#include "net/net_impl_esp32.h"
#endif

// --- Sync-task state, shared between the sync task and setup() -------------
static VerseData   g_verse    = {};
static WeatherData g_weather  = {};
static const char* g_offlineReason = nullptr;
static SemaphoreHandle_t g_syncDone = nullptr;

// Runs the whole network Sync phase on its own task/stack (see STACK NOTE
// above), then signals g_syncDone and deletes itself.
static void syncTask(void* /*arg*/) {
#ifdef CHROMAWOTD_NETWORK
    // cc_wifiConnect() internally checks for WIFI_SSID and returns 1 if not
    // configured, so we can call it unconditionally.
    Serial.println("sync: connecting wifi...");
    if (cc_wifiConnect() != 0) {
        g_offlineReason = "no wifi";
        Serial.println("sync: wifi FAILED (check secrets.h / signal)");
    } else {
        Serial.printf("sync: wifi OK, ip=%s\n", WiFi.localIP().toString().c_str());
        configTzTime("AEST-10AEDT,M10.1.0,M4.1.0/3", "pool.ntp.org");

        static VerseData fv;
        Serial.println("sync: fetching verse...");
        if (cc_fetchVerse(&fv) && fv.verse) {
            g_verse = fv;
            Serial.printf("sync: verse OK (%s)\n", fv.reference ? fv.reference : "?");
        } else {
            g_offlineReason = g_offlineReason ? g_offlineReason : "verse API";
            Serial.println("sync: verse FAILED");
        }
        Serial.println("sync: fetching weather...");
        if (!cc_fetchWeather(&g_weather)) {
            g_offlineReason = g_offlineReason ? g_offlineReason : "weather API";
            Serial.println("sync: weather FAILED");
        } else {
            Serial.printf("sync: weather OK (%.1f C, %s)\n", g_weather.temp,
                           g_weather.condition ? g_weather.condition : "?");
        }
    }
#else
    g_offlineReason = "network disabled in build";
#endif
    xSemaphoreGive(g_syncDone);
    vTaskDelete(nullptr);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("CHROMAWOTD %s boot\n", CHROMAWOTD_VERSION);
        delay(2000);   // room to attach a serial monitor before the sync log lines

    epaper.begin();
    epaper.setRotation(1);
    epaper.fillScreen(TFT_WHITE);

    // --- Sync (Phase 3): fetch real content on a big-stack task, wait. ------
    g_weather.temp = 0.0f;
    g_weather.icon = WeatherIcon::PartlyCloudy;

    g_syncDone = xSemaphoreCreateBinary();
    // 16 KB: mbedtls entropy/DRBG + HTTPClient + ArduinoJson easily exceed the
    // 8 KB default loopTask stack; this task owns its own arena so the rest of
    // the app is unaffected. Pinned to core 1 (same as loop) — no cross-core
    // surprises, just a bigger stack for this one call chain.
    xTaskCreatePinnedToCore(syncTask, "cc_sync", 16384, nullptr, 1, nullptr, 1);
    xSemaphoreTake(g_syncDone, portMAX_DELAY);
    vSemaphoreDelete(g_syncDone);

    VerseData& v = g_verse;
    WeatherData& w = g_weather;
    const char* offlineReason = g_offlineReason;

    // --- Fallback content (no network / no API) -----------------------------
    if (!v.verse) {
        static const char* fv =
            "Trust in the Lord with all your heart, and do not lean on your own "
            "understanding. In all your ways acknowledge him, and he will make "
            "straight your paths.";
        static const char* fref = "Proverbs 3:5-6";
        static const char* fdate = "Offline";
        v.verse = fv;
        v.reference = fref;
        v.date = fdate;
    }
    if (!w.condition) {
        static char cbuf[32];
        snprintf(cbuf, sizeof(cbuf), "Temp %.0f", (double)w.temp);
        w.condition = cbuf;
    }

    // --- Render -------------------------------------------------------------
    if (!verseHighlightFound(v)) {
        Serial.println("WARN: highlight phrase not found in verse - no red accent drawn");
    }
    if (offlineReason) {
        static char rbuf[NET_TEXT_MAX];
        snprintf(rbuf, sizeof(rbuf), "OFFLINE: %s", offlineReason);
        w.alert = rbuf;   // shows as the red alert in the weather column
    }

    drawLayout(v, w);
    epaper.update();
    Serial.println("CHROMAWOTD landscape layout pushed to display");
    epaper.sleep();

    // --- Deep sleep (Phase 4 wires a schedule; a robust hour for now). ------
    // ESP.deepSleep(3600e6);
}

void loop() {
    delay(1000);   // Phase 4 replaces this with timed deep-sleep wakeups
}