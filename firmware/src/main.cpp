// ChromaWOTD — 2.9" quad-colour ePaper Verse/Word of the Day + weather display.
// Seeed EE05 (XIAO ESP32-S3 Plus) + 2.9" BWRY ePaper (JD79661 panel / JD79667 driver IC).
//
// Behaviour:
//   * Sleeps between scheduled refreshes (06:00 / 12:30 / 18:00) and wakes on
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
#include "sched/hold_gesture.h"
#include "sched/wake_schedule.h"
#include "text/date_format.h"
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

// --- Wake-gesture classification ------------------------------------------------
// A button press is what WAKES the device, so the gesture must be classified by HOLD
// LENGTH at the start of setup(), before the serial delay: measured on hardware, the pads
// become observable 84 ms after the app starts and a TAP has released ~120 ms later, so any
// sampling that begins after the 2 s delay observes nothing at all (LESSONS §58).
//
// A double click was tried and ruled out by that measurement: every button wake — tap or
// double click — shows the pad still LOW at the first sample, because the first click is
// over before the firmware can look. Tap and double-click are indistinguishable here.
//
//   tap              -> normal sync + refresh
//   hold ~0.5-10 s   -> toggle the content mode (this wake only)
//   hold >= 10 s     -> factory reset
static HoldGesture sampleWakeGesture() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1)
        return HoldGesture::None; // timer or power-on: there is no gesture to classify

    ensureButtonPadsReady();
    // The session logic lives in hold_gesture.cpp so it is unit-tested: an earlier version of
    // this loop discarded the ShortHold the classifier reported while the pad was still down,
    // and short holds were silently ignored on hardware (LESSONS §59).
    HoldSession s;
    cc_holdSessionBegin(&s);
    uint32_t prev = millis();
    for (;;) {
        const uint32_t now = millis();
        const HoldGesture g = cc_holdSessionFeed(&s, cc_anyButtonDown(), now - prev);
        prev = now;
        if (g != HoldGesture::None)
            return g;
        delay(CC_HOLD_SAMPLE_MS);
    }
}

// Did a short-hold gesture ask to see the OTHER content? RTC memory, so the choice survives
// deep sleep; the next SCHEDULED wake clears it (see setup()).
RTC_DATA_ATTR static uint8_t g_contentInvert = 0;
RTC_DATA_ATTR static uint8_t g_contentInvertValid = 0;

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
static char g_date[32] = "";    // "DOW DD MMM" for the header (text/date_format.h)
static char g_isoDate[11] = ""; // "YYYY-MM-DD" local date, for comparing against a source's edition
// True when the Word-of-the-Day page's own edition date is NOT the device's local date.
// A.Word.A.Day publishes at 00:01 US Eastern (14:01 AEST / 15:01 AEDT), so the 12:30 local
// slot reads the PREVIOUS edition — the same word shown at the previous 18:00 slot. Without
// this the panel looks like it is repeating itself for no reason.
static bool g_wordStaleEdition = false;

// Had this device been configured before? RTC memory survives DEEP SLEEP — the mode this
// device spends its life in — so this catches a configuration wipe across a sleep (the
// failure mode in LESSONS §45) but NOT across a power cycle, which legitimately looks like
// a first boot. Its only job is to make a silent loss *explained* on the panel instead of
// indistinguishable from a fresh setup.
RTC_DATA_ATTR static uint32_t g_wasProvisioned = 0;

#ifdef CHROMAWOTD_WIFI_DIAG
// --- Wi-Fi reachability diagnostic (env:wifidiag) -----------------------------
// A join failing with WL_NO_SSID_AVAIL means the SSID was never SEEN in a scan, which
// is a completely different fault from a refused association. This separates the two
// candidate causes — no AP in range, versus an RF chain whose calibration was destroyed
// — and reports the shared partition's per-namespace entry counts, because a reformat
// (BUG-06's own mechanism) takes the Wi-Fi stack's `phy` / `nvs.net80211` data with it.
// That is the cost side of the fix: our config is protected, the Wi-Fi stack's is not.
//
// Prints NO credential material. The configured SSID is reported only as
// visible/not-visible, never by name.
#include <nvs.h>

static void runWifiDiag() {
    Serial.println("=== CHROMAWOTD WIFI DIAG ===");

    nvs_stats_t st = {};
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        Serial.printf("diag: shared nvs: used=%u free=%u total=%u namespaces=%u\n", (unsigned)st.used_entries,
                      (unsigned)st.free_entries, (unsigned)st.total_entries, (unsigned)st.namespace_count);
    } else {
        Serial.println("diag: shared nvs: stats unavailable");
    }

    static const char* kNss[] = {"nvs.net80211", "phy", "misc", "dhcp_state", "bss"};
    for (const char* ns : kNss) {
        nvs_handle_t h;
        if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
            size_t used = 0;
            if (nvs_get_used_entry_count(h, &used) == ESP_OK)
                Serial.printf("diag: namespace %-13s present, entries used=%u\n", ns, (unsigned)used);
            else
                Serial.printf("diag: namespace %-13s present, entry count unavailable\n", ns);
            nvs_close(h);
        } else {
            Serial.printf("diag: namespace %-13s ABSENT\n", ns);
        }
    }

    WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    const char* want = cc_configActive().ssid;
    bool foundOwn = false;
    for (int i = 0; i < n && !foundOwn; i++) {
        if (want[0] && WiFi.SSID(i) == want)
            foundOwn = true;
    }
    Serial.printf("diag: scan found %d AP(s); our configured SSID visible = %s\n", n, foundOwn ? "YES" : "NO");
    if (n == 0)
        Serial.println("diag: NO APs visible at all -> the RF chain is the problem, not "
                       "the credentials");
    else if (!foundOwn)
        Serial.println("diag: other APs visible but ours is not -> the AP is off / out of range");
    else
        Serial.println("diag: our SSID IS visible -> the fault is in associating, not in scanning");
    Serial.println("=== end WIFI DIAG ===");
    Serial.flush();
}
#endif

// Fetch the Verse of the Day into g_verse. Called both for a genuine verse refresh and for
// a Word refresh whose edition is stale (see g_wordStaleEdition) — the panel then shows the
// verse rather than repeating a word the reader has already seen.
static void syncVerseFallback() {
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

// Runs the whole network Sync phase on its own task/stack (see STACK NOTE
// above), then signals g_syncDone and deletes itself.
static void syncTask(void* /*arg*/) {
#ifdef CHROMAWOTD_NETWORK
#ifdef CHROMAWOTD_WIFI_DIAG
    runWifiDiag();
#endif
    Serial.println("sync: connecting wifi...");
    if (cc_wifiConnect() != 0) {
        g_offlineReason = "no wifi";
        // No network means no reading at all: invalidate the weather so the panel does
        // not render a defaulted 0°C as if it were a measurement.
        g_weather.valid = false;
        g_weather.condition = nullptr;
        Serial.println("sync: wifi FAILED (check Wi-Fi details / signal)");
    } else {
        Serial.printf("sync: wifi OK, host=%s ip=%s\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());
        configTzTime(activeTz(), "pool.ntp.org");

        // Bounded NTP wait: the content mode + weather window depend on it.
        struct tm tmv{};
        if (getLocalTime(&tmv, 6000)) {
            g_haveTime = true;
            // Formatted through the SHARED helper: the render harness is given the same
            // string, so a render can no longer look right while the panel says something
            // else (the ISO "2026-09-19" this used to build was never what the previews showed).
            cc_formatHeaderDate(tmv.tm_wday, tmv.tm_mday, tmv.tm_mon + 1, g_date, sizeof(g_date));
            // The local date as ISO, used only to decide whether a fetched word is TODAY's
            // edition (the source stamps each page with its own edition date).
            snprintf(g_isoDate, sizeof(g_isoDate), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
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
        // A short-hold gesture inverts that decision for this refresh, so the user can see
        // the other content without changing any setting.
        if (g_contentInvertValid && g_contentInvert)
            g_mode = cc_invertContentMode(g_mode);
        g_tomorrow = g_haveTime ? cc_useTomorrowForecast(tmv.tm_hour) : false;
        Serial.printf("sync: mode=%s weather=%s\n", g_mode == ContentMode::Word ? "word" : "verse",
                      g_tomorrow ? "tomorrow" : "today");

        if (g_mode == ContentMode::Word) {
            static WordData wd;
            Serial.println("sync: fetching word of the day...");
            if (cc_fetchWord(&wd) && wd.definition) {
                g_word = wd;
                // Is this TODAY's edition by the device's own calendar? The source stamps
                // each page with its edition date (00:01 US Eastern). A mismatch means the
                // 12:30 slot is holding yesterday's word — the one the previous 18:00 slot
                // already showed.
                g_wordStaleEdition = (g_isoDate[0] && wd.editionDate && strcmp(wd.editionDate, g_isoDate) != 0);
                Serial.printf("sync: word OK (%s) edition=%s local=%s\n", wd.word ? wd.word : "?",
                              wd.editionDate ? wd.editionDate : "?", g_isoDate[0] ? g_isoDate : "?");
                if (g_wordStaleEdition) {
                    // The source has not published today's edition yet (it publishes at
                    // 00:01 US Eastern = 14:01 AEST / 15:01 AEDT, after the 12:30 slot).
                    // Showing it again would read as a broken device: the reader saw this
                    // exact word at the previous evening's refresh. Show the verse instead;
                    // the word appears fresh at the next slot past the source's day boundary.
                    Serial.println("sync: word edition is YESTERDAY's -> showing the verse this refresh");
                    g_mode = ContentMode::Verse;
                    syncVerseFallback();
                }
            } else {
                // Online but this API failed -> partial, not offline. NO invented word:
                // g_word.definition stays null and the render path shows an explicit
                // "no word" state rather than a bundled word pretending to be today's.
                g_partialReason = g_partialReason ? g_partialReason : "word API";
                Serial.println("sync: word FAILED (no content to show)");
            }
        } else {
            syncVerseFallback();
        }

        Serial.println("sync: fetching weather...");
        if (!cc_fetchWeather(&g_weather, g_tomorrow)) {
            // Mark the reading invalid so nothing draws a fabricated temperature. The
            // struct is zero-initialised, so temp would read 0.0 — a confident and
            // wrong "0°C" is worse than admitting there is no reading.
            g_weather.valid = false;
            g_weather.condition = nullptr;
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
// Establishes BUG-06 two independent ways (LESSONS §45/§48):
//
//   (1) STRUCTURAL, always conclusive: ask the partition table which partition the
//       core would erase, and which partition holds our config. The core finds its
//       erase target with a NULL label -- esp_partition_find_first(DATA, NVS, NULL),
//       i.e. the FIRST data/nvs entry. If our config is in a different partition,
//       that erase cannot reach it.
//   (2) BEHAVIOURAL: fill the SHARED `nvs` until writes fail, reboot so
//       nvs_flash_init() runs against a full partition, then report whether the fill
//       markers were wiped and whether our config survived.
//
// Run as a PAIR: env:nvsprobe_legacy (old table) vs env:nvsprobe (new table).
//
// STAGE PERSISTENCE -- the bug in the first version of this probe. RTC_DATA_ATTR
// lives in `.rtc.data`, which IS re-initialised on every boot except a deep-sleep
// wake; it therefore did NOT survive the ESP.restart() below, the stage reset to 0,
// and the probe filled and rebooted the device forever. `.rtc_noinit`
// (RTC_NOINIT_ATTR) is the section left alone across a reset. It is uninitialised on
// a cold boot, so it is validated with a magic.
#include <Preferences.h>
#include <esp_partition.h>
#include <nvs.h>

RTC_NOINIT_ATTR static uint32_t g_nvsProbeMagic;
RTC_NOINIT_ATTR static uint32_t g_nvsProbeStage;
static const uint32_t kProbeMagic = 0xC0FFEE42u;

static uint32_t probeStage() {
    if (g_nvsProbeMagic != kProbeMagic || g_nvsProbeStage > 1)
        return 0; // cold boot: .rtc_noinit holds arbitrary values
    return g_nvsProbeStage;
}

static void setProbeStage(uint32_t s) {
    g_nvsProbeMagic = kProbeMagic;
    g_nvsProbeStage = s;
}

static void cc_sharedNvsStats(const char* when) {
    nvs_stats_t st = {};
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        Serial.printf("probe: %s shared-nvs: used=%u free=%u total=%u namespaces=%u\n", when, (unsigned)st.used_entries,
                      (unsigned)st.free_entries, (unsigned)st.total_entries, (unsigned)st.namespace_count);
    } else {
        Serial.printf("probe: %s shared-nvs: stats unavailable\n", when);
    }
}

// (1) Structural: can the core's erase reach the partition our config lives in?
static void cc_structuralCheck() {
    const esp_partition_t* eraseTarget =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    const esp_partition_t* cfgPart =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs_cfg");

    Serial.println("probe: --- structural check (the core erases the FIRST data/nvs entry) ---");
    if (eraseTarget) {
        Serial.printf("probe: core would erase : label=%-8s offset=0x%06x size=%u B\n", eraseTarget->label,
                      (unsigned)eraseTarget->address, (unsigned)eraseTarget->size);
    } else {
        Serial.println("probe: core would erase : <none found>");
    }
    if (cfgPart) {
        Serial.printf("probe: our config lives : label=%-8s offset=0x%06x size=%u B\n", cfgPart->label,
                      (unsigned)cfgPart->address, (unsigned)cfgPart->size);
    } else {
        Serial.println("probe: our config lives : <NO 'nvs_cfg' partition in this table>");
    }

    if (!cfgPart || cfgPart == eraseTarget) {
        Serial.println("probe: STRUCTURAL VERDICT = VULNERABLE - our config shares the partition the "
                       "core erases");
    } else {
        Serial.println("probe: STRUCTURAL VERDICT = SEPARATED - the core's erase target is a "
                       "different partition, so it cannot touch our config");
    }
}

// Are our fill markers still present? Absent => the shared partition was reformatted.
static bool cc_fillMarkersPresent() {
    Preferences p;
    if (!p.begin("nvsfill", /*readOnly=*/true))
        return false;
    const bool any = p.isKey("f0000") || p.isKey("f0001") || p.isKey("f0010");
    p.end();
    return any;
}

static void runNvsFillProbe() {
    const uint32_t stage = probeStage();
    Serial.println();
    Serial.println("=== CHROMAWOTD NVS FILL PROBE (destructive) ===");
    Serial.printf("probe: stage=%u (0=fill, 1=report)\n", (unsigned)stage);
    Serial.printf("probe: config provisioned=%s\n", cc_configIsProvisioned(g_cfg) ? "YES" : "no");
    cc_sharedNvsStats("at-start:");
    cc_structuralCheck();

    if (stage == 0) {
        if (!cc_configIsProvisioned(g_cfg)) {
            Serial.println("probe: ABORT - device is not provisioned, so there is no config to lose "
                           "and the result would be meaningless. Configure it first.");
            Serial.flush();
            return;
        }
        Preferences p;
        if (!p.begin("nvsfill", /*readOnly=*/false)) {
            Serial.println("probe: ABORT - could not open the fill namespace");
            Serial.flush();
            return;
        }
        const size_t freeBefore = p.freeEntries();
        Serial.printf("probe: Preferences::freeEntries() before fill = %u\n", (unsigned)freeBefore);
        Serial.println("probe: filling the SHARED 'nvs' partition until writes fail...");

        uint32_t written = 0;
        size_t last = 1;
        uint32_t failedAt = 0;
        for (uint32_t i = 0; i < 4000; i++) {
            char key[16];
            snprintf(key, sizeof(key), "f%05u", (unsigned)i);
            last = p.putUChar(key, (uint8_t)i);
            if (last != 1) {
                failedAt = i;
                break;
            }
            written++;
        }
        // Read a key straight back. The first version reported "332 keys accepted" while
        // nvs_get_stats showed NO change in used entries; those cannot both be true, so
        // the fill is now measured rather than assumed.
        const uint8_t firstVal = p.getUChar("f00000", 0xAA); // 0xAA = "missing"
        const bool readBack = (firstVal == 0);
        const size_t freeAfter = p.freeEntries();
        p.end();

        Serial.printf("probe: wrote %u keys, first failure at index %u (putUChar -> %u)\n", (unsigned)written,
                      (unsigned)failedAt, (unsigned)last);
        Serial.printf("probe: first key read-back = %s\n",
                      readBack ? "OK (writes persisted)" : "MISMATCH (writes did not persist)");
        Serial.printf("probe: freeEntries() after fill = %u (was %u)\n", (unsigned)freeAfter, (unsigned)freeBefore);
        cc_sharedNvsStats("filled:");

        setProbeStage(1);
        Serial.println("probe: rebooting so the core's nvs_flash_init() runs on this partition");
        Serial.flush();
        delay(200);
        ESP.restart(); // does not return
    }

    // --- Stage 1: nvs_flash_init() has already run during THIS boot. --------------
    const bool markers = cc_fillMarkersPresent();
    const bool provisioned = cc_configIsProvisioned(g_cfg);
    cc_sharedNvsStats("after:");
    Serial.printf("probe: fill markers still present = %s\n", markers ? "YES" : "no");
    Serial.printf("probe: OUR CONFIG still present    = %s\n", provisioned ? "YES" : "no");
    if (markers) {
        Serial.println("probe: BEHAVIOURAL VERDICT = INCONCLUSIVE - the fill did not force a "
                       "reformat (markers survived)");
    } else if (provisioned) {
        Serial.println("probe: BEHAVIOURAL VERDICT = shared partition REFORMATTED and our config "
                       "SURVIVED => BUG-06 is FIXED");
    } else {
        Serial.println("probe: BEHAVIOURAL VERDICT = our config was DESTROYED with the shared "
                       "partition => BUG-06 REPRODUCED (pre-fix behaviour)");
    }
    Serial.println("=== end NVS fill probe; staying awake so the result can be read ===");
    Serial.println("probe: power-cycle (or reflash) to run the fill again; the stage survives a reset");
    Serial.flush();

    // LINGER rather than sleep. A probe that deep-sleeps within a second of printing has an
    // awake window too short to attach a serial capture to (the first version of this ran an
    // endless fill loop, which is the only reason it was readable at all). This is a USB bench
    // diagnostic, so staying awake costs nothing and the verdict can be read at any time.
    for (;;) {
        delay(10000);
        Serial.printf("probe: [still] markers=%s config=%s\n", markers ? "PRESENT" : "wiped",
                      provisioned ? "intact" : "DESTROYED");
    }
}
#endif

#ifdef CHROMAWOTD_GESTURE_PROBE
// --- Gesture-timing probe (env:gestureprobe) -----------------------------------
// Answers ONE question with hardware rather than arithmetic: after an ext1 wake, how
// soon can the firmware observe the pads at all — and is a natural double click still
// in progress by then?
//
// Why it exists: the press IS the wake source, so nothing watches the pad until this
// code runs. The ROM bootloader (unavoidable) plus the 2 s serial delay in setup() (which
// exists so the boot log survives a cold plug-in) may consume the whole gesture. This
// samples the RTC pads as the FIRST executable statement, before any delay and before
// the panel is initialised, then logs every edge with a microsecond timestamp.
//
// Repeatable: after the watch window it deep-sleeps with a short timer AND ext1 armed, so
// each new double-click is another wake event. Run it, click a dozen times, read the log.
static void runGestureProbe() {
    // Sample FIRST — before Serial, before the panel. micros() is already running (the
    // Arduino core initialises the timer before setup()), so this timestamp is our only
    // origin: it says how long from the app's start, not from the physical press.
    const uint32_t tFirst = micros();
    int initial[3];
    for (int i = 0; i < kButtonCount; i++) {
        gpio_num_t pin = kButtonPins[i];
        rtc_gpio_init(pin);
        rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_dis(pin);
        rtc_gpio_pullup_en(pin);
        initial[i] = rtc_gpio_get_level(pin);
    }

    Serial.begin(115200);
    Serial.println();
    Serial.println("=== GESTURE PROBE ===");
    Serial.printf("wake cause %d (%s)\n", (int)esp_sleep_get_wakeup_cause(),
                  esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ? "button" : "not a button");
    Serial.printf("first pad sample at t=%u us\n", (unsigned)tFirst);
    Serial.printf("pads at first sample: GPIO2=%d GPIO3=%d GPIO8=%d  (0 = STILL HELD)\n", initial[0], initial[1],
                  initial[2]);
    Serial.println("watching for edges for 10 s - double-click now if you have not already");
    Serial.println();

    // Count a "second press" as any transition to LOW that happens AFTER the first sample,
    // which is what a double click would look like from here.
    int pressesAfterFirstSample = 0;
    uint32_t lastEdgeUs = 0;
    int last[3];
    for (int i = 0; i < kButtonCount; i++)
        last[i] = rtc_gpio_get_level(kButtonPins[i]);

    const uint32_t start = micros();
    while (micros() - start < 10ULL * 1000000ULL) {
        for (int i = 0; i < kButtonCount; i++) {
            const int v = rtc_gpio_get_level(kButtonPins[i]);
            if (v != last[i]) {
                const uint32_t t = micros() - start;
                Serial.printf("[t=%7u us] GPIO%d %d -> %d  %s", (unsigned)t, (int)kButtonPins[i], last[i], v,
                              v == 0 ? "PRESS\n" : "release\n");
                if (v == 0) {
                    pressesAfterFirstSample++;
                    lastEdgeUs = t;
                }
                last[i] = v;
            }
        }
        delay(1); // 1 ms polling: fine resolution, no busy-wait
    }

    Serial.println();
    Serial.printf("VERDICT: %d press(es) observed AFTER the first sample", pressesAfterFirstSample);
    if (initial[0] == 0 || initial[1] == 0 || initial[2] == 0)
        Serial.println("  (a pad was ALREADY low at the first sample - the click was still held)");
    else if (pressesAfterFirstSample > 0)
        Serial.printf("  (last at t=%u us - the gesture was still in progress)\n", (unsigned)lastEdgeUs);
    else
        Serial.println("  (no edges at all - the whole gesture finished BEFORE the firmware looked;"
                       " a double click is NOT observable at this boot latency)");
    Serial.println("=== END GESTURE PROBE: sleeping ~20 s, then double-click again ===");
    Serial.flush();

    uint64_t mask = 0;
    for (int i = 0; i < kButtonCount; i++)
        if (rtc_gpio_get_level(kButtonPins[i]) == 1)
            mask |= (1ULL << (int)kButtonPins[i]);
    if (mask)
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup(20ULL * 1000000ULL);
    esp_deep_sleep_start(); // does not return
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
#ifdef CHROMAWOTD_GESTURE_PROBE
    runGestureProbe(); // never returns; measures the pad-observation window at a wake
#endif

    // Classify the wake gesture BEFORE the serial delay — a tap is over in ~120 ms
    // (see sampleWakeGesture). The delay itself stays: it exists so the boot log survives a
    // cold plug-in, and the log is a first-class test artifact for this project.
    const HoldGesture gesture = sampleWakeGesture();

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
    if (cc_configIsProvisioned(g_cfg))
        g_wasProvisioned = 1;

#ifdef CHROMAWOTD_NVS_FILL_PROBE
    runNvsFillProbe(); // env:nvsprobe / env:nvsprobe_legacy; never returns on stage 0
#endif

    // --- Wake gesture: long hold = factory reset, short hold = toggle content ----
    // The gesture itself was classified before the serial delay; this applies its effects.
    // A SCHEDULED wake always returns to time-based content, so a toggle can never leave the
    // device stuck showing the wrong half of the day.
#ifdef CHROMAWOTD_BUTTON_WAKE
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        g_contentInvertValid = 0;

    if (gesture == HoldGesture::Reset) {
        Serial.println("gesture: long hold -> wiping stored configuration");
        cc_configEraseNvs();
        g_wasProvisioned = 0; // deliberate wipe: not a loss to report
        g_contentInvertValid = 0;
        // Rebuild from defaults so the wipe is visible immediately, then fall into the
        // setup portal below (the SSID will be empty).
        cc_configInit();
    } else if (gesture == HoldGesture::ShortHold) {
        g_contentInvert = g_contentInvert ? 0 : 1;
        g_contentInvertValid = 1;
        Serial.printf("gesture: short hold -> showing the other content (invert=%u)\n", (unsigned)g_contentInvert);
    } else if (gesture == HoldGesture::Tap) {
        Serial.println("gesture: tap -> normal refresh");
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
        // Explain a LOSS rather than presenting a first boot: if RTC memory says this device
        // had been configured, the settings went away without a factory reset (LESSONS §45).
        const bool lost = (g_wasProvisioned != 0);
        if (lost)
            Serial.println("setup: WARN no stored configuration, but this device WAS provisioned before");
        cc_portalDrawScreen(pinfo, lost ? PortalNotice::SettingsLost : PortalNotice::None,
                            lost ? "The saved settings are gone. Please set up again." : nullptr);
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
        static char body[WORD_BODY_MAX];
        // Composed by the SHARED helper so the preview tool renders the same string this
        // device does. The old local assembly capped the body at NET_TEXT_MAX (230 bytes)
        // with snprintf, which cut the usage example off mid-sentence — and because the
        // shortened text then FIT the block, the renderer drew no overflow marker, so the
        // panel looked like a complete example that simply ended (LESSONS §65).
        cc_composeWordBody(g_word, body, sizeof(body));

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
