// CHROMAWOTD — 2.9" quad-colour ePaper Verse/Word of the Day + weather display.
// Seeed EE05 (XIAO ESP32-S3 Plus) + 2.9" BWRY ePaper (JD79661 panel / JD79667 driver IC).
//
// Behaviour:
//   * Sleeps between scheduled refreshes (06:30 / 12:30 / 18:00) and wakes on
//     the RTC timer OR any of the three user buttons (BUTTON1/2/3 = GPIO2/3/5,
//     active-low) — a button press runs exactly the same sync+render cycle.
//   * Content by local time: 00:00–11:59 Verse of the Day, 12:00–23:59 Word of
//     the Day (Word from A.Word.A.Day, with the respelling pronunciation).
//   * Weather column by local time: 18:00–23:59 shows TOMORROW's outlook and
//     captions it "TOMORROW", otherwise today's under "FORECAST".
//   * Falls back to bundled content + a red OFFLINE banner when the network or
//     an API is unavailable. See docs/ARCHITECTURE.md for the state machine.
//
// STACK NOTE: mbedtls's entropy gathering + CTR-DRBG reseed during the first
// TLS handshake needs more stack than the default Arduino loopTask (8 KB on
// this core) provides — it overflows partway through start_ssl_client(). This
// project ships the precompiled Arduino-ESP32 core (not an IDF component
// build), so framework sdkconfig.h already `#define`s CONFIG_MAIN_TASK_STACK_SIZE
// and a build_flags override is silently shadowed. The portable fix: run the
// whole Sync phase (Wi-Fi + TLS fetch) on a dedicated FreeRTOS task with an
// explicit 16 KB stack, and have setup() block on a semaphore until it finishes.

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <time.h>
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
#include "sched/wake_schedule.h"
#include "sched/content_policy.h"

#ifdef CHROMAWOTD_NETWORK
#include "net/net_impl_esp32.h"
#endif

// POSIX TZ string (Sydney/Melbourne, DST-aware).
static const char kTzPosix[] = "AEST-10AEDT,M10.1.0,M4.1.0/3";

// Fallback sleep when the wall clock isn't trustworthy: retry soon rather than
// sleeping until a bogus "next slot". Never sleep less than this (wake-loop guard).
static const uint64_t kFallbackSleepSec = 3600ULL;
static const int      kMinSleepSec      = 60;

// EE05 user buttons: BUTTON1/2/3 = D1/D2/D4 = GPIO2/3/5, active-low. All three
// are RTC-capable, so a shared ext1 (all-low) mask can wake the chip.
static const gpio_num_t kButtonPins[] = { GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_5 };
static const int        kButtonCount  = sizeof(kButtonPins) / sizeof(kButtonPins[0]);

// --- Sync-task state, shared between the sync task and setup() -------------
static VerseData   g_verse    = {};
static WordData    g_word     = {};
static WeatherData g_weather  = {};
static const char* g_offlineReason = nullptr;
static SemaphoreHandle_t g_syncDone = nullptr;
static ContentMode g_mode     = ContentMode::Verse;
static bool        g_tomorrow = false;
static bool        g_haveTime = false;
static char        g_date[32] = "";   // "YYYY-MM-DD" for the header

// Runs the whole network Sync phase on its own task/stack (see STACK NOTE
// above), then signals g_syncDone and deletes itself.
static void syncTask(void* /*arg*/) {
#ifdef CHROMAWOTD_NETWORK
    Serial.println("sync: connecting wifi...");
    if (cc_wifiConnect() != 0) {
        g_offlineReason = "no wifi";
        Serial.println("sync: wifi FAILED (check secrets.h / signal)");
    } else {
        Serial.printf("sync: wifi OK, host=%s ip=%s\n",
                      WiFi.getHostname(), WiFi.localIP().toString().c_str());
        configTzTime(kTzPosix, "pool.ntp.org");

        // Bounded NTP wait: the content mode + weather window depend on it.
        struct tm tmv;
        if (getLocalTime(&tmv, 6000)) {
            g_haveTime = true;
            snprintf(g_date, sizeof(g_date), "%04d-%02d-%02d",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
            Serial.printf("sync: time OK %s %02d:%02d:%02d\n", g_date,
                          tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        } else {
            Serial.println("sync: WARN ntp time not available");
        }

        // Without a clock we cannot pick content by time: default to the verse
        // and today's weather rather than showing the wrong thing.
        g_mode     = g_haveTime ? cc_contentModeForHour(tmv.tm_hour) : ContentMode::Verse;
        g_tomorrow = g_haveTime ? cc_useTomorrowForecast(tmv.tm_hour) : false;
        Serial.printf("sync: mode=%s weather=%s\n",
                      g_mode == ContentMode::Word ? "word" : "verse",
                      g_tomorrow ? "tomorrow" : "today");

        if (g_mode == ContentMode::Word) {
            static WordData wd;
            Serial.println("sync: fetching word of the day...");
            if (cc_fetchWord(&wd) && wd.definition) {
                g_word = wd;
                Serial.printf("sync: word OK (%s)\n", wd.word ? wd.word : "?");
            } else {
                g_offlineReason = g_offlineReason ? g_offlineReason : "word API";
                Serial.println("sync: word FAILED (bundled fallback)");
            }
        } else {
            static VerseData fv;
            Serial.println("sync: fetching verse...");
            if (cc_fetchVerse(&fv) && fv.verse) {
                g_verse = fv;
                Serial.printf("sync: verse OK (%s)\n", fv.reference ? fv.reference : "?");
            } else {
                g_offlineReason = g_offlineReason ? g_offlineReason : "verse API";
                Serial.println("sync: verse FAILED");
            }
        }

        Serial.println("sync: fetching weather...");
        if (!cc_fetchWeather(&g_weather, g_tomorrow)) {
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

// Bundled word used when the A.Word.A.Day fetch/parse fails, so the afternoon
// panel still shows something coherent (flagged by the OFFLINE banner).
static const char* kFallbackWord = "serendipity";
static const char* kFallbackPron = "(ser-uhn-DIP-i-tee)";
static const char* kFallbackDef  =
    "noun: The occurrence and development of events by chance in a happy or beneficial way.";

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("CHROMAWOTD %s boot\n", CHROMAWOTD_VERSION);
    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    Serial.printf("wake cause: %d (%s)\n", (int)wakeCause,
                  wakeCause == ESP_SLEEP_WAKEUP_TIMER ? "timer" :
                  wakeCause == ESP_SLEEP_WAKEUP_EXT1  ? "button" :
                  wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED ? "power-on/reset" : "other");
#ifdef CHROMAWOTD_DEBUG_DELAY
    delay(3000);   // development only: time to attach a serial monitor
#endif

    epaper.begin();
    epaper.setRotation(1);
    epaper.fillScreen(TFT_WHITE);

    // --- Sync (Phase 3): fetch content on a big-stack task, wait for it. ----
    g_weather.temp = 0.0f;
    g_weather.icon = WeatherIcon::PartlyCloudy;

    g_syncDone = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(syncTask, "cc_sync", 16384, nullptr, 1, nullptr, 1);
    xSemaphoreTake(g_syncDone, portMAX_DELAY);
    vSemaphoreDelete(g_syncDone);

    // --- Assemble the view --------------------------------------------------
    LayoutOptions opts;
    VerseData v = {};

    if (g_mode == ContentMode::Word) {
        // Word of the Day: definition + example in the body, the respelling
        // pronunciation as the black left caption, the headword as the red right
        // caption. Notes: the IPA-style pronunciation is pre-respelled by the
        // source, so it is plain ASCII and safe for the panel font.
        static char body[NET_TEXT_MAX];
        const char* word = (g_word.definition ? g_word.word : kFallbackWord);
        const char* def  = (g_word.definition ? g_word.definition : kFallbackDef);
        const char* ex   = (g_word.definition ? g_word.example : nullptr);
        if (ex && ex[0]) snprintf(body, sizeof(body), "%s  %s", def, ex);
        else             snprintf(body, sizeof(body), "%s", def);

        v.verse     = body;
        v.highlight = nullptr;
        v.reference = word;                       // red, right
        v.date      = g_haveTime ? g_date : "Word of the Day";
        opts.headerTitle = "Word of the Day";
        opts.leftCaption = g_word.pronunciation ? g_word.pronunciation : kFallbackPron;
        if (!g_word.definition) opts.leftCaption = kFallbackPron;
    } else {
        if (g_verse.verse) {
            v = g_verse;
        } else {
            static const char* fv =
                "Trust in the Lord with all your heart, and do not lean on your own "
                "understanding. In all your ways acknowledge him, and he will make "
                "straight your paths.";
            static const char* fref = "Proverbs 3:5-6";
            v.verse = fv;
            v.reference = fref;
            v.date = g_haveTime ? g_date : "Offline";
        }
        opts.headerTitle = "Verse of the Day";
    }

    WeatherData& w = g_weather;
    if (!w.condition) {
        static char cbuf[32];
        snprintf(cbuf, sizeof(cbuf), "Temp %.0f", (double)w.temp);
        w.condition = cbuf;
    }
    opts.weatherLabel = g_tomorrow ? "TOMORROW" : "FORECAST";

    // --- Render -------------------------------------------------------------
    if (!verseHighlightFound(v)) {
        Serial.println("NOTE: no highlight phrase in this content (expected for Word of the Day)");
    }
    if (g_offlineReason) {
        static char rbuf[NET_TEXT_MAX];
        snprintf(rbuf, sizeof(rbuf), "OFFLINE: %s", g_offlineReason);
        w.alert = rbuf;   // shows as the red alert in the weather column
    }

    drawLayout(v, w, opts);
    epaper.update();
    Serial.println("CHROMAWOTD layout pushed to display");
    epaper.sleep();

    // --- Sleep until the next slot, or until a button is pressed ------------
#ifdef CHROMAWOTD_DEEP_SLEEP
    setenv("TZ", kTzPosix, 1);   // ensure localtime() works even if NTP failed
    tzset();

    uint64_t sleepSec = kFallbackSleepSec;
    struct tm tmNow;
    if (getLocalTime(&tmNow, 100)) {
        int s = cc_secondsUntilNextWake(tmNow);
        if (s < kMinSleepSec) s = kMinSleepSec;
        sleepSec = (uint64_t)s;
    }
#ifdef CHROMAWOTD_WAKE_TEST_SEC
    sleepSec = CHROMAWOTD_WAKE_TEST_SEC;
#endif

    // Button wake: enable RTC pull-ups (digital pulls are lost in deep sleep)
    // and arm the shared ext1 mask. A press then wakes us for a normal sync.
    uint64_t btnMask = 0;
    for (int i = 0; i < kButtonCount; i++) {
        rtc_gpio_pullup_en(kButtonPins[i]);
        rtc_gpio_pulldown_dis(kButtonPins[i]);
        btnMask |= (1ULL << (int)kButtonPins[i]);
    }
    esp_sleep_enable_ext1_wakeup(btnMask, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);
    Serial.printf("awake-status: wake_cause=%d, sleeping %llu s (or on button)\n",
                  (int)wakeCause, (unsigned long long)sleepSec);
    Serial.flush();
    esp_deep_sleep_start();   // does not return
#else
    Serial.println("sleep: CHROMAWOTD_DEEP_SLEEP not set - idling (debug build)");
#endif
}

void loop() {
    // Only reached when deep sleep is disabled (debug): stay idle, never redraw.
    delay(1000);
}
