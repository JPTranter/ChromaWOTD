// CHROMAWOTD — 2.9" quad-colour ePaper Verse/Word of the Day + weather display.
// Seeed EE05 (XIAO ESP32-S3 Plus) + 2.9" BWRY ePaper (JD79661 panel / JD79667 driver IC).
//
// Behaviour:
//   * Sleeps between scheduled refreshes (06:30 / 12:30 / 18:00) and wakes on
//     the RTC timer OR any of the three user buttons (BUTTON1/2/3 = GPIO2/3/8,
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
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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
#error \
    "EPAPER_ENABLE not set - check BOARD_SCREEN_COMBO / USE_XIAO_EPAPER_DISPLAY_BOARD_EE05 build_flags in platformio.ini"
#endif

#include "net/net.h"
#include "sched/content_policy.h"
#include "sched/factory_reset.h"
#include "sched/wake_schedule.h"
#include "verse_display.h"

#include "config/config.h"
#include "config/config_active.h"
#include "net/portal.h"

#ifdef CHROMAWOTD_NETWORK
#include "net/net_impl_esp32.h"
#endif

// POSIX TZ string (Sydney/Melbourne, DST-aware). Superseded at runtime by the
// configured timezone (NVS -> built-in default); see config/config.h.
static const char kTzPosixDefault[] = "AEST-10AEDT,M10.1.0,M4.1.0/3";

// The device's resolved configuration (NVS -> built-in defaults) lives in
// config_active.cpp as a single instance; g_cfg is a convenience reference to it so
// this file reads naturally without copying the struct around.
static DeviceConfig& g_cfg = *cc_configMutable();

// POSIX TZ for the current boot, DERIVED from the configured IANA name.
//
// This must never return the raw stored value: the stored value is an IANA name
// (e.g. "Australia/Melbourne") which newlib cannot resolve without tzdata, and an
// incomplete POSIX string is silently wrong (no DST rule -> newlib assumes US dates
// -> the clock is an hour out and the 06:30 wake fires at 05:30). cc_configResolvedTz
// translates the name to a complete rule; an unrecognised value falls back to the
// built-in default rather than being passed through.
static const char* activeTz() {
    const char* resolved = cc_configResolvedTz(g_cfg);
    return resolved ? resolved : kTzPosixDefault;
}

// Fallback sleep when the wall clock isn't trustworthy: retry soon rather than
// sleeping until a bogus "next slot". Never sleep less than this (wake-loop guard).
static const uint64_t kFallbackSleepSec = 3600ULL;
static const int kMinSleepSec = 60;

// EE05 user buttons, established on hardware with the env:probe diagnostic
// (2026-09-12) rather than from the schematic — the schematic's net labels are
// ambiguous with -layout extraction and the third button is NOT where the first
// reading suggested:
//   BUTTON1 = D1 = GPIO2, BUTTON2 = D2 = GPIO3, BUTTON3 = D9 = GPIO8
// All three are active-low and RTC-capable, so one ext1 ANY_LOW mask wakes
// the chip (a press pulls its own pin low; the other two stay high). Arming
// GPIO5/D4 (the schematic's "I2C_SDA" net, not a button) was
// what caused the deep-sleep wake storm — see LESSONS §34.
static const gpio_num_t kButtonPins[] = {GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_8};
static const int kButtonCount = sizeof(kButtonPins) / sizeof(kButtonPins[0]);

// --- Button sampling for the factory-reset gesture ---------------------------
// These MUST use the RTC domain. The buttons are armed as RTC pull-ups before
// sleep, and pinMode()/digitalRead() hand the pad back to the digital domain,
// undoing the pull-up (LESSONS §34) — a digital read here would report the wrong
// level and the gesture would never register.
//
// Configure the pads once (idempotent) so a boot that never sleeps still reads them
// correctly.
static void ensureButtonPadsReady() {
    static bool done = false;
    if (done)
        return;
    for (int i = 0; i < kButtonCount; i++) {
        rtc_gpio_init(kButtonPins[i]);
        rtc_gpio_set_direction(kButtonPins[i], RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_dis(kButtonPins[i]);
        rtc_gpio_pullup_en(kButtonPins[i]);
    }
    done = true;
}

// True when at least one button is currently held down (active-low).
static bool cc_anyButtonDown() {
    ensureButtonPadsReady();
    for (int i = 0; i < kButtonCount; i++)
        if (rtc_gpio_get_level(kButtonPins[i]) == 0)
            return true;
    return false;
}

// True when a button was already down at wake. On an ext1 wake the asserting pin is
// necessarily low, so this is a cheap gate that avoids sampling the whole 10 s hold
// window on every button press.
static bool cc_latchedButtonHeld() {
    return cc_anyButtonDown();
}

// Safety net: consecutive button-wake cycles observed, kept in RTC memory so it
// survives deep sleep. If a button line misbehaves (floats, or is stuck low) the
// device would otherwise wake -> sync -> wake forever; after kExt1StreakLimit
// such cycles we stop arming ext1 for one cycle and fall back to timer-only.
RTC_DATA_ATTR static uint32_t g_ext1Streak = 0;
static const uint32_t kExt1StreakLimit = 5;

// --- Sync-task state, shared between the sync task and setup() -------------
static VerseData g_verse = {};
static WordData g_word = {};
static WeatherData g_weather = {};
static const char* g_offlineReason = nullptr; // non-null when the NETWORK was unreachable
static const char* g_partialReason = nullptr; // non-null when a single API failed while online
static SemaphoreHandle_t g_syncDone = nullptr;
static ContentMode g_mode = ContentMode::Verse;
static bool g_tomorrow = false;
static bool g_haveTime = false;
static char g_date[32] = ""; // "YYYY-MM-DD" for the header

// Runs the whole network Sync phase on its own task/stack (see STACK NOTE
// above), then signals g_syncDone and deletes itself.
static void syncTask(void* /*arg*/) {
#ifdef CHROMAWOTD_NETWORK
    Serial.println("sync: connecting wifi...");
    if (cc_wifiConnect() != 0) {
        g_offlineReason = "no wifi";
        // No network means no reading at all: invalidate the weather so the panel does
        // not render a defaulted 0°C as if it were a measurement.
        g_weather.valid = false;
        g_weather.condition = nullptr;
        g_weather.icon = WeatherIcon::PartlyCloudy;
        Serial.println("sync: wifi FAILED (check Wi-Fi details / signal)");
    } else {
        Serial.printf("sync: wifi OK, host=%s ip=%s\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());
        configTzTime(activeTz(), "pool.ntp.org");

        // Bounded NTP wait: the content mode + weather window depend on it.
        struct tm tmv{};
        if (getLocalTime(&tmv, 6000)) {
            g_haveTime = true;
            snprintf(g_date, sizeof(g_date), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
            Serial.printf("sync: time OK %s %02d:%02d:%02d\n", g_date, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        } else {
            // Without wall time the clock sits at 1970 — BEFORE the notBefore of every
            // certificate in any chain — so every TLS handshake below fails
            // verification and the fetches look like an API outage. Name the cause
            // rather than letting it be misread as the servers being down.
            Serial.println("sync: WARN ntp time not available");
            Serial.println("sync: WARN clock is unset - TLS certificate validation cannot succeed "
                           "(no cert is valid at 1970); fetches will be reported as failed");
        }

        // Content follows the time of day unless the setup portal pinned a mode
        // (contentMode 1 = verse only, 2 = word only); see cc_resolveContentMode.
        // Without a clock the time-based rule cannot run, so it falls back to the
        // verse rather than showing the wrong half of the day's content.
        g_mode = cc_resolveContentMode(g_cfg.contentMode, g_haveTime, g_haveTime ? tmv.tm_hour : 0);
        g_tomorrow = g_haveTime ? cc_useTomorrowForecast(tmv.tm_hour) : false;
        Serial.printf("sync: mode=%s weather=%s\n", g_mode == ContentMode::Word ? "word" : "verse",
                      g_tomorrow ? "tomorrow" : "today");

        if (g_mode == ContentMode::Word) {
            static WordData wd;
            Serial.println("sync: fetching word of the day...");
            if (cc_fetchWord(&wd) && wd.definition) {
                g_word = wd;
                Serial.printf("sync: word OK (%s)\n", wd.word ? wd.word : "?");
            } else {
                // Online but this API failed -> partial, not offline. NO invented word:
                // g_word.definition stays null and the render path shows an explicit
                // "no word" state rather than a bundled word pretending to be today's.
                g_partialReason = g_partialReason ? g_partialReason : "word API";
                Serial.println("sync: word FAILED (no content to show)");
            }
        } else {
            static VerseData fv;
            Serial.println("sync: fetching verse...");
            if (cc_fetchVerse(&fv) && fv.verse) {
                g_verse = fv;
                Serial.printf("sync: verse OK (%s)\n", fv.reference ? fv.reference : "?");
            } else {
                g_partialReason = g_partialReason ? g_partialReason : "verse API";
                Serial.println("sync: verse FAILED (no content to show)");
            }
        }

        Serial.println("sync: fetching weather...");
        if (!cc_fetchWeather(&g_weather, g_tomorrow)) {
            // Mark the reading invalid so nothing draws a fabricated temperature. The
            // struct is zero-initialised, so temp would read 0.0 — a confident and
            // wrong "0°C" is worse than admitting there is no reading.
            g_weather.valid = false;
            g_weather.condition = nullptr;
            g_weather.icon = WeatherIcon::PartlyCloudy;
            g_partialReason = g_partialReason ? g_partialReason : "weather API";
            Serial.println("sync: weather FAILED (no reading)");
        } else {
            g_weather.valid = true;
            Serial.printf("sync: weather OK (%.1f C, %s)\n", g_weather.temp,
                          g_weather.condition ? g_weather.condition : "?");
        }
    }
#else
    g_offlineReason = "no network in this build";
    g_weather.valid = false;
#endif
    xSemaphoreGive(g_syncDone);
    vTaskDelete(nullptr);
}

// There is deliberately NO bundled verse/word fallback. Inventing content (a canned
// `serendipity` or Proverbs 3:5-6) presented as "the Word/Verse of the Day" made the
// device look like it was working when it was not — the panel asserted a specific
// daily reading it had no way to know. A failed fetch now produces an explicit
// "unavailable" screen instead.
//
// (Cached last-good content, shown with an "as of" note, is the right long-term answer
// and is still on PROJECT_PLAN — it is real data rather than invented data.)

#ifdef CHROMAWOTD_BUTTON_PROBE
// Bench diagnostic (env:probe): identify which pads the EE05's three side
// buttons are actually wired to, and their polarity. Deep sleep is never
// entered — this watches the candidate pads and prints every transition, so
// each button can be pressed in turn and matched to a line. Exists because
// arming ext1 on the schematic's D1/D2/D4 caused a deep-sleep wake storm
// (LESSONS §34): the assumed pin map/polarity is demonstrably wrong.
static void runButtonProbe() {
    struct Probe {
        int gpio;
        const char* label;
    };
    // XIAO ESP32-S3 D-pin map, minus the native-USB pads (GPIO19/20 = D-/D+,
    // which must not be reconfigured or the USB CDC link drops) and the
    // flash/PSRAM pads. D1/D2 already confirmed as BUTTON1/BUTTON2 (active-low);
    // D4 did NOT respond, so the third button is somewhere else in this set.
    const Probe pins[] = {
        {0, "GPIO0  (XIAO BOOT)"}, {1, "GPIO1  (D0/BAT_ADC)"}, {2, "GPIO2  (D1)  [BTN1]"}, {3, "GPIO3  (D2)  [BTN2]"},
        {4, "GPIO4  (D3)"},        {5, "GPIO5  (D4)"},         {6, "GPIO6  (D5)"},         {7, "GPIO7  (D8)"},
        {8, "GPIO8  (D9)"},        {9, "GPIO9  (D10)"},        {21, "GPIO21 (D?)"},        {43, "GPIO43 (D6/TX)"},
        {44, "GPIO44 (D7/RX)"},
    };
    const int n = (int)(sizeof(pins) / sizeof(pins[0]));
    int last[16];

    Serial.println();
    Serial.println("=== CHROMAWOTD BUTTON PROBE ===");
    Serial.println("All pads are INPUT_PULLUP: an UNPRESSED button reads 1.");
    Serial.println("A press pulls its pad to 0 (active-low).");
    Serial.println();
    for (int i = 0; i < n; i++) {
        pinMode(pins[i].gpio, INPUT_PULLUP);
        last[i] = digitalRead(pins[i].gpio);
        Serial.printf("  %-22s idle=%d\n", pins[i].label, last[i]);
    }
    Serial.println();
    Serial.println("watching for level changes - press each side button in turn...");

    while (true) {
        for (int i = 0; i < n; i++) {
            int v = digitalRead(pins[i].gpio);
            if (v != last[i]) {
                Serial.printf("[%7lu ms] %-22s %d -> %d  %s\n", (unsigned long)millis(), pins[i].label, last[i], v,
                              v == 0 ? "<== PRESSED (active-low)" : "released");
                last[i] = v;
            }
        }
        delay(15);
    }
}
#endif

#ifdef CHROMAWOTD_NVS_FILL_PROBE
// --- Destructive NVS-fill diagnostic (env:nvsprobe / env:nvsprobe_legacy) -------
// Reproduces BUG-06 (LESSONS §45) deterministically and reports what survived.
//
// The fault: the Arduino core's initArduino() erases a whole partition when
// nvs_flash_init() returns ESP_ERR_NVS_NO_FREE_PAGES, and it picks that partition with
// a NULL label — i.e. the FIRST `data, nvs` entry in the table, which on the legacy
// layout is the 20 KB `nvs` shared with the WiFi stack. Fill that partition and the
// credentials stored in it are gone with it.
//
// Run it as a PAIR, which is the whole point — this demonstrates the fix instead of
// asserting it:
//   env:nvsprobe_legacy -> config shares `nvs`          -> config DESTROYED
//   env:nvsprobe        -> config in its own `nvs_cfg`  -> config SURVIVES
//
// Stage 0: fill the shared namespace until writes fail, then reboot so the core's
//          nvs_flash_init() runs against a genuinely full partition.
// Stage 1: report the damage and the verdict, then sleep (no portal, no ~25 s sweep) so
//          the diagnostic stays short and re-runnable.
#include <Preferences.h>
#include <nvs.h>

RTC_DATA_ATTR static uint32_t g_nvsFillStage = 0;

static void cc_nvsStats(const char* when) {
    nvs_stats_t st = {};
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        Serial.printf("probe: %s shared-nvs entries used=%u free=%u total=%u namespaces=%u\n", when,
                      (unsigned)st.used_entries, (unsigned)st.free_entries, (unsigned)st.total_entries,
                      (unsigned)st.namespace_count);
    } else {
        Serial.printf("probe: %s shared-nvs stats unavailable\n", when);
    }
}

// Are our fill markers still there? Absent => the shared partition was reformatted.
static bool cc_fillMarkersPresent() {
    Preferences p;
    if (!p.begin("nvsfill", /*readOnly=*/true))
        return false;
    const bool any = p.isKey("f0000") || p.isKey("f0001") || p.isKey("f0010");
    p.end();
    return any;
}

static void runNvsFillProbe() {
    Serial.println();
    Serial.println("=== CHROMAWOTD NVS FILL PROBE (destructive) ===");
    Serial.printf("probe: stage=%u  (0=fill, 1=report)\n", (unsigned)g_nvsFillStage);
    Serial.printf("probe: config provisioned=%s\n", cc_configIsProvisioned(g_cfg) ? "YES" : "no");
    cc_nvsStats("before:");

    if (g_nvsFillStage == 0) {
        if (!cc_configIsProvisioned(g_cfg)) {
            // Without a stored configuration there is nothing to lose, so the result
            // would be meaningless and would look like a false "destroyed" verdict.
            Serial.println("probe: ABORT - the device is not provisioned. Configure it via the "
                           "setup portal first, or the test proves nothing.");
            Serial.flush();
            return;
        }
        Preferences p;
        if (!p.begin("nvsfill", /*readOnly=*/false)) {
            Serial.println("probe: ABORT - could not open the fill namespace");
            Serial.flush();
            return;
        }
        Serial.println("probe: filling the SHARED 'nvs' partition until writes fail...");
        uint32_t written = 0;
        size_t last = 0;
        for (uint32_t i = 0; i < 20000; i++) {
            char key[16];
            snprintf(key, sizeof(key), "f%05u", (unsigned)i);
            last = p.putUChar(key, (uint8_t)i);
            if (last != 1)
                break;
            written++;
        }
        p.end();
        cc_nvsStats("filled:");
        Serial.printf("probe: %u keys accepted, then putUChar returned %u\n", (unsigned)written,
                      (unsigned)last);
        g_nvsFillStage = 1;
        Serial.println("probe: rebooting so the core's nvs_flash_init() sees a full partition...");
        Serial.flush();
        delay(200);
        ESP.restart(); // does not return
    }

    // --- Stage 1: nvs_flash_init() has already run during THIS boot. --------------
    const bool markers = cc_fillMarkersPresent();
    const bool provisioned = cc_configIsProvisioned(g_cfg);
    cc_nvsStats("after:");
    Serial.printf("probe: fill markers still present=%s\n", markers ? "YES" : "no");
    Serial.printf("probe: OUR CONFIG still present=%s\n", provisioned ? "YES" : "no");
    if (markers) {
        Serial.println("probe: VERDICT = INCONCLUSIVE - the shared partition was not full "
                       "enough to force a reformat");
    } else if (provisioned) {
        Serial.println("probe: VERDICT = shared partition REFORMATTED and our config SURVIVED "
                       "=> BUG-06 is FIXED");
    } else {
        Serial.println("probe: VERDICT = our config was DESTROYED along with the shared "
                       "partition => BUG-06 REPRODUCED (pre-fix behaviour)");
    }
    Serial.println("=== end NVS fill probe: sleeping; reflash to run again ===");
    Serial.flush();
#ifdef CHROMAWOTD_DEEP_SLEEP
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
    esp_deep_sleep_start(); // does not return
#endif
}
#endif

// --- Deep-sleep arming, shared by the normal cycle and the setup-portal path ---
// Never returns in a deep-sleep build. In a debug build (CHROMAWOTD_DEEP_SLEEP
// unset) it logs and returns, leaving loop() to idle without redrawing.
//
// Extracted so the expired-setup-portal path can sleep with the SAME wake sources
// as a normal cycle — otherwise an unprovisioned device would either stay awake
// forever or sleep with no wake armed.
static void armSleepAndSleep(esp_sleep_wakeup_cause_t wakeCause) {
#ifdef CHROMAWOTD_DEEP_SLEEP
    setenv("TZ", activeTz(), 1); // ensure localtime() works even if NTP failed
    tzset();

    uint64_t sleepSec = kFallbackSleepSec;
    struct tm tmNow;
    if (getLocalTime(&tmNow, 100)) {
        int s = cc_secondsUntilNextWake(tmNow);
        if (s < kMinSleepSec)
            s = kMinSleepSec;
        sleepSec = (uint64_t)s;
    }
#ifdef CHROMAWOTD_WAKE_TEST_SEC
    sleepSec = CHROMAWOTD_WAKE_TEST_SEC;
#endif

    // Button wake: BUTTON1/2/3 = GPIO2/3/8, active-low.
    //
    // EXPERIMENTAL — gated behind CHROMAWOTD_BUTTON_WAKE (off by default) until
    // the pin behaviour is confirmed on real hardware. Enabling it on a board
    // where these pads do not idle high produces a wake storm (boot -> sync ->
    // wake instantly -> repeat), because a level-triggered ext1 low-level wake
    // fires immediately if any armed pin already reads low.
    //
    // Correct configuration requires ALL of:
    //   * rtc_gpio_init() + RTC_GPIO_MODE_INPUT_ONLY so the pad is owned by the
    //     RTC domain (rtc_gpio_pullup_en() is a no-op otherwise),
    //   * reading the level via rtc_gpio_get_level() — NOT digitalRead(), since
    //     pinMode() hands the pad back to the digital domain and undoes the RTC
    //     pull-up,
    //   * arming only pins that actually idle HIGH.
#ifdef CHROMAWOTD_BUTTON_WAKE
    uint64_t btnMask = 0;
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1)
        g_ext1Streak++;
    else
        g_ext1Streak = 0;
    if (g_ext1Streak >= kExt1StreakLimit) {
        Serial.printf("button wake suppressed for one cycle (%lu consecutive button wakes) - timer only\n",
                      (unsigned long)g_ext1Streak);
        g_ext1Streak = 0;
    }
    for (int i = 0; i < kButtonCount && g_ext1Streak < kExt1StreakLimit; i++) {
        gpio_num_t pin = kButtonPins[i];
        rtc_gpio_init(pin);
        rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_dis(pin);
        rtc_gpio_pullup_en(pin);
        bool released = (rtc_gpio_get_level(pin) == 1);
        Serial.printf("button GPIO%d idle=%s\n", (int)pin, released ? "HIGH (armed)" : "LOW (floating? NOT armed)");
        if (released)
            btnMask |= (1ULL << (int)pin);
    }
    if (btnMask) {
        // ANY_LOW, not ALL_LOW: the chip wakes when ANY armed pin goes low, which
        // is what a single button press does. Only pins that idle HIGH are armed
        // above, so a press pulls exactly one pin low and wakes the chip.
        //
        // ESP_EXT1_WAKEUP_ALL_LOW is deprecated on ESP32-S3 and is defined as an
        // alias of ANY_LOW (=0) — the legacy ALL_LOW semantics (wake only when ALL
        // selected pins are simultaneously low) exist on the original ESP32 alone.
        // Using the alias spammed -Wdeprecated-declarations, and naming it ALL_LOW
        // misdescribed the hardware-verified behaviour (a single press wakes).
        esp_sleep_enable_ext1_wakeup(btnMask, ESP_EXT1_WAKEUP_ANY_LOW);
    } else {
        Serial.println("button wake disabled (no pin idles HIGH) - timer only");
    }
#else
    Serial.println("button wake: not enabled in this build (timer-only sleep)");
#endif
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);
    Serial.printf("awake-status: wake_cause=%d, sleeping %llu s (or on button)\n", (int)wakeCause,
                  (unsigned long long)sleepSec);
    Serial.flush();
    esp_deep_sleep_start(); // does not return
#else
    (void)wakeCause;
    Serial.println("sleep: CHROMAWOTD_DEEP_SLEEP not set - idling (debug build)");
#endif
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("CHROMAWOTD %s boot\n", CHROMAWOTD_VERSION);

#ifdef CHROMAWOTD_BUTTON_PROBE
    runButtonProbe(); // never returns; nothing else in setup() runs
#endif

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    Serial.printf("wake cause: %d (%s)\n", (int)wakeCause,
                  wakeCause == ESP_SLEEP_WAKEUP_TIMER       ? "timer"
                  : wakeCause == ESP_SLEEP_WAKEUP_EXT1      ? "button"
                  : wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED ? "power-on/reset"
                                                            : "other");
#ifdef CHROMAWOTD_DEBUG_DELAY
    delay(3000); // development only: time to attach a serial monitor
#endif

    epaper.begin();
    epaper.setRotation(1);
    epaper.fillScreen(TFT_WHITE);

    // --- Configuration: NVS (from the portal) over built-in defaults. ----------
    const bool hadNvs = cc_configInit(); // fills the shared instance
    Serial.printf("config: source=%s ssid=%s tz=%s\n", hadNvs ? "nvs" : "compile-time",
                  g_cfg.ssid[0] ? "(set)" : "(empty)", // never log the SSID itself
                  g_cfg.timezone);

#ifdef CHROMAWOTD_NVS_FILL_PROBE
    runNvsFillProbe(); // env:nvsprobe / env:nvsprobe_legacy; never returns on stage 0
#endif

    // --- Factory reset: a button that stays held through the wake --------------
    // Only meaningful on a BUTTON wake; a timer wake has nobody holding anything.
#ifdef CHROMAWOTD_BUTTON_WAKE
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1 && cc_latchedButtonHeld()) {
        ResetHoldState hold;
        cc_resetHoldBegin(&hold);
        bool reset = false;
        Serial.printf("reset: button held, checking for %u ms...\n", (unsigned)CC_RESET_HOLD_MS);
        while (!reset) {
            if (cc_resetHoldSample(&hold, cc_anyButtonDown())) {
                reset = true;
                break;
            }
            if (!cc_anyButtonDown()) {
                Serial.println("reset: released early - normal refresh");
                break;
            }
            delay(CC_RESET_SAMPLE_MS);
        }
        if (reset) {
            Serial.println("reset: wiping stored configuration");
            cc_configEraseNvs();
            // Rebuild from defaults so the wipe is visible immediately, then fall
            // into the setup portal below (the SSID will be empty).
            cc_configInit();
        }
    }
#endif

    // --- First-boot setup portal ----------------------------------------------
    // No usable network config (nothing in NVS and nothing compiled in) means the
    // device cannot do anything useful, so raise the SoftAP wizard instead of
    // showing fallback content forever.
#ifdef CHROMAWOTD_NETWORK
    if (!cc_configIsProvisioned(g_cfg)) {
        PortalInfo pinfo;
        cc_portalMakeInfo((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), esp_random(), &pinfo);
        cc_portalDrawScreen(pinfo, nullptr);
        epaper.update(); // panel is the only channel the user can read
        epaper.sleep();

        Serial.printf("setup: starting portal '%s'\n", pinfo.apName);
        // BOUNDED setup window. An unconfigured device must not stay awake forever:
        // this is a battery-powered ambient device, so leaving the SoftAP and the
        // CPU running indefinitely would flatten the pack, and an AP that never
        // closes is a needless exposure. If the window expires we deep-sleep, which
        // also means the whole cycle (portal -> sleep -> portal on the next wake)
        // is the same shape as normal operation and the user can simply try again.
        const bool configured = cc_portalRun(pinfo, CC_PORTAL_TIMEOUT_MS);
        if (configured) {
            Serial.println("setup: configured - rebooting into normal operation");
            delay(500);
            ESP.restart();
        }
        // The window closed without configuration. Do NOT fall through into the
        // sync/render path below: the device is still unprovisioned, so the sync
        // fails and repaints the panel OVER the setup screen — wiping the QR code
        // and the per-boot AP password (which stay readable at zero power) with an
        // OFFLINE error. Sleep instead; the next wake (a button press, or the next
        // scheduled slot) re-opens the portal with a freshly generated credential.
        Serial.println("setup: window expired, sleeping (press a button to retry)");
        armSleepAndSleep(wakeCause);
        return; // debug build only (no deep sleep): idle in loop(), keep the setup screen
    }
#endif

    // --- Sync (Phase 3): fetch content on a big-stack task, wait for it. ----
    g_weather.temp = 0.0f;
    g_weather.icon = WeatherIcon::PartlyCloudy;

    g_syncDone = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(syncTask, "cc_sync", 16384, nullptr, 1, nullptr, 1);
    xSemaphoreTake(g_syncDone, portMAX_DELAY);
    vSemaphoreDelete(g_syncDone);

    // --- Assemble the view --------------------------------------------------
    // No content means an explicit "unavailable" screen, never invented text. The old
    // behaviour substituted a canned verse/word, so a broken fetch was invisible.
    LayoutOptions opts;
    VerseData v = {};
    const bool haveContent =
        (g_mode == ContentMode::Word) ? (g_word.definition != nullptr) : (g_verse.verse != nullptr);

    if (!haveContent) {
        // A clear, unambiguous failure screen. The reason is carried by the weather
        // column's alert so the two halves of the panel agree about what went wrong.
        static char unavailable[NET_TEXT_MAX];
        if (g_offlineReason) {
            // Whole-network failure: tell the user what to do about it.
            snprintf(unavailable, sizeof(unavailable),
                     "No network connection. Check the Wi-Fi details, or move the device "
                     "closer to the router.");
        } else {
            // Online, but this particular API failed.
            snprintf(unavailable, sizeof(unavailable),
                     "Today's content could not be fetched. The device will try again at "
                     "the next scheduled refresh.");
        }
        v.verse = unavailable;
        v.reference = nullptr;
        v.date = g_haveTime ? g_date : "";
        opts.headerTitle = (g_mode == ContentMode::Word) ? "Word unavailable" : "Verse unavailable";
        opts.leftCaption = nullptr;
    } else if (g_mode == ContentMode::Word) {
        // Word of the Day: definition + example in the body, the respelling
        // pronunciation as the black left caption, the headword as the red right
        // caption. Notes: the IPA-style pronunciation is pre-respelled by the
        // source, so it is plain ASCII and safe for the panel font.
        static char body[NET_TEXT_MAX];
        const char* ex = g_word.example;
        if (ex && ex[0])
            snprintf(body, sizeof(body), "%s  %s", g_word.definition, ex);
        else
            snprintf(body, sizeof(body), "%s", g_word.definition);

        v.verse = body;
        v.highlight = nullptr;
        v.reference = g_word.word; // red, right
        v.date = g_haveTime ? g_date : "";
        opts.headerTitle = "Word of the Day";
        opts.leftCaption = g_word.pronunciation;
    } else {
        v = g_verse;
        opts.headerTitle = "Verse of the Day";
    }

    WeatherData& w = g_weather;
    if (w.valid && !w.condition) {
        static char cbuf[32];
        snprintf(cbuf, sizeof(cbuf), "Temp %.0f", (double)w.temp);
        w.condition = cbuf;
    }
    opts.weatherLabel = g_tomorrow ? "TOMORROW" : "FORECAST";

    // --- Render -------------------------------------------------------------
    if (!verseHighlightFound(v) && v.highlight) {
        // A highlight was supplied and NOT found: worth surfacing, since the red accent
        // would otherwise be silently missing. (No highlight at all is normal for the
        // Word of the Day, so that case is not a warning.)
        Serial.println("WARN: highlight phrase not found in the content");
    }

    // The alert explains the failure. Only one alert can be shown, so a total network
    // failure takes precedence over a single-API failure.
    if (g_offlineReason) {
        static char rbuf[NET_TEXT_MAX];
        snprintf(rbuf, sizeof(rbuf), "OFFLINE: %s", g_offlineReason);
        w.alert = rbuf; // red alert in the weather column
    } else if (g_partialReason) {
        static char rbuf[NET_TEXT_MAX];
        // When the clock never synced, a failed fetch is almost certainly TLS
        // rejecting a 1970 "now", not an API outage. Say which, so the panel is not
        // actively misleading about whose fault it is.
        if (!g_haveTime)
            snprintf(rbuf, sizeof(rbuf), "PARTIAL: %s failed (device clock not set)", g_partialReason);
        else
            snprintf(rbuf, sizeof(rbuf), "PARTIAL: %s failed", g_partialReason);
        w.alert = rbuf;
    }

    drawLayout(v, w, opts);
    epaper.update();
    Serial.println("CHROMAWOTD layout pushed to display");
    epaper.sleep();

    // --- Sleep until the next slot, or until a button is pressed ------------
    armSleepAndSleep(wakeCause); // does not return in a deep-sleep build
}

void loop() {
    // Only reached when deep sleep is disabled (debug): stay idle, never redraw.
    delay(1000);
}
