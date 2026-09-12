# CHROMAWOTD — Project Plan

## Concept

A 2.9" 4-colour ePaper display showing the **Verse of the Day** (or, on toggle, a
**Word of the Day**) plus colour-coded local weather on an infrequent full refresh
schedule. Colour is the point: red/yellow carry meaning (alerts, citations,
highlights, warmth), not decoration. There is no clock — the panel cannot refresh
often enough for one, so content is designed for 2–4 full sweeps per day.

## Hardware

- Seeed 2.9" Quadruple Color ePaper, 128×296, JD79661, SPI (SKU 104990855)
- XIAO ePaper Display Board EE05 + XIAO ESP32-S3
- USB-powered initially; EE05 JST battery later

## Phases

### Phase 0 — Scaffold (complete)
- [x] Repo structure, PlatformIO env for XIAO ESP32-S3
- [x] First successful `pio run -e s3` build

### Phase 1 — Display bring-up & Layout Prototype (complete)
- [x] Verify EE05 pin mapping and use official Seeed GFX `BOARD_SCREEN_COMBO 512`
- [x] Full refresh test pattern in all 4 colours (~25 s confirmed)
- [x] Hardware verification: the 2.9" BWRY panel does not support partial refresh
      (Seeed's 104990855 datasheet lists the panel IC as JD79661; Seeed GFX drives it
      through its JD79667 code path for combo 512)
- [x] Architectural pivot: infrequent glanceable layout (Daily Verse + Weather)
- [x] Dual-target layout engine (`verse_display.cpp` / `.h`)
- [x] Host test harness & mock canvas generating PNG screenshots (single landscape layout)
- [x] Layout hardening: shared UTF-8→ASCII glyph normalisation, visible `...` overflow
      markers, negative-temperature rounding, case-insensitive highlight matching
- [x] Regression suite with region invariants (`test_layout_overflow`, 11 invariant tests)

### Phase 2 — Setup Wizard, Hardware Buttons & State Persistence
- [ ] **First-Boot Setup Screen**: Display on-screen instructions guide (SSID, AP IP address `192.168.4.1`, setup steps).
- [ ] **Captive Portal Wi-Fi Wizard**: SoftAP mode + web configuration portal for SSID/password, timezone, location, and preferred content mode.
- [ ] **Factory Reset**: Long-press button hold (10 seconds) detection to wipe NVS credentials and reboot into setup wizard.
- [x] **Button Hardware Interaction & Deep Sleep Wake** (Phase 5, 2026-09-12):
  - [x] `ext1` multi-button deep sleep wake mask — **BUTTON1/2/3 = GPIO2/3/5** (D1/D2/D4),
        active-low. (Corrected: D0/GPIO1 is `BAT_ADC`, not a button, and GPIO0 is the
        XIAO BOOT strap — the earlier guess was wrong. See LESSONS §33.)
  - [x] All three buttons run a full immediate re-sync + refresh (same path as a timer wake).
  - [x] Content mode is time-based, not toggled: Verse 00:00–11:59, Word 12:00–23:59.
  - [ ] Lockout during active ~25s screen sweep to ignore switch bounce/spam.
- [ ] **Non-Volatile State Persistence (`Preferences` / NVS)**:
  - Wi-Fi credentials, timezone POSIX string, latitude/longitude.
  - Active display layout & content mode.
  - Cached last successful verse, word, and weather data with timestamp.

### Phase 3 — Weather, Dual Content Sources & Sync Indicators
- [ ] **Syncing & Status Feedback**:
  - Hardware user LED (`LED_BUILTIN` / GPIO 21) active pulse during Wi-Fi connection and API fetch (avoiding unneeded 25s screen updates).
  - Diagnostic LED blink cadences on connection failure.
  - [x] Graceful failure state: if Wi-Fi / API times out, render fallback content with a
        prominent red banner (`OFFLINE: [Reason]`).
  - Exponential / 15-minute retry backoff on network failure before re-entering sleep.
- [x] **NTP Synchronization**: SNTP sync on wake, POSIX timezone adjustment with automatic DST handling.
- [x] **Weather Pipeline**: Open-Meteo REST fetch, ArduinoJson parsing, weather code to BWRY icon mapping, alert detection. (Evening syncs pull the *next* day's daily entry.)
- **Content Pipelines**:
  - [x] Verse of the Day pipeline (BibleGateway VOTD).
  - [x] Word of the Day pipeline — **A.Word.A.Day** (`wordsmith.org/words/today.html`):
        definition + usage example, respelling pronunciation, headword. (Wordnik /
        Merriam-Webster abandoned: M-W is Cloudflare-walled, Wordnik needs a key.)

### Phase 4 — Daily Wake Schedule & Power Optimization
- [x] **Time-Based Wake Schedule**:
  - [x] Deep sleep scheduled wakeups: Morning (06:30), Midday (12:30), Evening (18:00);
        the device sleeps through to the next slot (~3 sweeps/day, not continuous).
  - [x] RTC drift correction against NTP (`configTzTime` + bounded `getLocalTime` each wake;
        1 h fallback sleep if the clock is not yet valid).
  - Pure schedule math in `firmware/src/sched/wake_schedule.{h,cpp}` (`test_sched.cpp`).
- [ ] **Battery Monitoring & Low-Battery Cutoff**:
  - Analog voltage sampling via divider on RTC ADC pin.
  - Low battery warning icon in header (at < 15% / ~3.55V).
  - Critical cutoff screen (< 3.3V): Display persistent "LOW BATTERY — PLEASE RECHARGE" screen and enter infinite deep sleep with display power gated off.
- [ ] **Enclosure sketch**: Reuse 79 × 36.7 mm panel cutout dimensions from eClock (500mAh LiPo flat pouch fit).
- [ ] **Release Tagging & CI**: Tag-triggered binary builds.


## Known unknowns (Resolved)

- **Display driver**: Resolved. Seeed GFX (`BOARD_SCREEN_COMBO 512`) provides exact pin
  mappings and timing for the EE05 + 2.9" BWRY panel. Naming note: Seeed's datasheet says
  the panel IC is JD79661, while the library's combo-512 setup lives in
  `TFT_Drivers/JD79667_Defines.h` / `JD79667_Init.h`. The library is the operative truth
  for the build; do not "fix" either name without checking the other.
- **EE05 pin mapping**: Resolved. SCLK 7(D8), MOSI 9(D10), CS 44(D7), DC 10(D16), BUSY 4(D3),
  RST 38(D11), ENABLE 43(D6).
- **Partial-refresh support**: Resolved. The 2.9" quad-colour panel does NOT support
  partial refresh. Every refresh is a full ~25-second multi-pass sweep. System design
  updates 2–4 times per day rather than every minute.

## Open follow-ups (from the 2026-09-12 project review)

- [x] ~~Portrait strip/verse-block issues~~ **Resolved 2026-09-12**: portrait mode is
      dropped entirely; the device has one landscape, light presentation. This removed the
      cramped alert strip and verse-block dead-space problems by removal, and also cleared
      the theme-dispatcher inconsistency (REVIEW D3) and most of the per-variant geometry
      duplication.
- [ ] The `drawWrappedText#` and `drawVerseBlock` helpers are only used by the single
      layout now; a block-descriptor type could still remove the remaining repeated
      geometry constants, but it is lower priority than it was with five layouts.

