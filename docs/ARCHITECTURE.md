# ChromaWOTD — Architecture

**Last updated:** 2026-09-19
**Firmware version:** 0.1.1
**Phase:** 5 — Buttons, time-based content, device configuration (NVS) & setup portal (complete)

---

## Overview

ChromaWOTD is a 2.9" quad-colour ePaper display (BWRY: black, white, red, yellow) showing:
- **Verse of the Day** (scripture) or **Word of the Day** (vocabulary) — chosen by TIME OF DAY
  (00:00–14:59 verse, 15:00–23:59 word — see `kCcWordOfDayStartHour`), or pinned to one of the two
  from the setup portal.
  A tap runs a refresh; a **hold of ~0.5-10 s inverts the content mode for that refresh**
  (and the next scheduled wake returns to time-based); a hold of 10 s+ is the factory reset.
  The gesture is classified by HOLD LENGTH, because a double click turned out to be
  indistinguishable from a single tap at this boot latency — measured, see LESSONS §58.
- **Local weather** (temperature + condition as TEXT, in the footer row; warnings in red)
- **Date** in the header band

**Presentation** — the panel is used to give the text maximum room:

| zone | contents |
|---|---|
| header band (18 px, yellow) | **what** you are reading: the citation (`Proverbs 3:5-6`), or the word with its respelling (`breviloquent (bre-VIL-uh-kwuhnt)`), drawn one size up at 7pt; the date sits at the right at the **same size**, formatted `DOW DD MMM` (`Sat 19 Sep`) by `text/date_format.h`. The respelling is never dropped — it shrinks (7 → 6 → 5.5 → 5pt) into the room left before the date and is truncated only as a last resort (LESSONS §65) |
| body | the verse / definition, **full panel width**, auto-sized by a ladder from 10pt down to 5pt (the 5pt rung is what a paragraph-length Word-of-the-Day example needs). The block is the space between the header rule and the footer row — 22 → 115 px — and holds only whole lines (`cc_lineCapacity = maxH / lineHeight`) |
| footer row | status / warnings at the LEFT in red; weather as text at the RIGHT (`25°C Partly cloudy`) |

There is deliberately **no weather column and no mode title**: the mode name was the least
informative text on the panel, and the 70 px the weather column occupied cost the verse a type
size (LESSONS §53).

The device refreshes **2–4 times per day** (full ~25 s pigment sweep per refresh). Between refreshes, the ESP32-S3 enters deep sleep to conserve power.

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  External APIs (HTTPS)                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ BibleGateway │  │ Open-Meteo   │  │ NTP (time)   │          │
│  │ Verse JSON   │  │ Weather JSON │  │ Time Sync    │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                 │                 │                   │
│         ▼                 ▼                 ▼                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Network Layer (Phase 3+)                               │   │
│  │  - WiFiClientSecure + setCACert() for TLS               │   │
│  │  - HTTPClient / ArduinoJson parsing                     │   │
│  │  - Credentials stored in NVS (not plaintext)            │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                             │                                   │
│                             ▼                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Data Layer (Phase 3+)                                  │   │
│  │  VerseData { date, verse, highlight, reference }        │   │
│  │  WeatherData { temp, condition, alert }                │   │
│  │  Cached last successful fetch + timestamp               │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                             │                                   │
│                             ▼                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Layout Engine (Phase 1, dual-target)                   │   │
│  │  verse_display.cpp + text/ + draw/ modules              │   │
│  │  - UTF-8 → ASCII normalisation (text/glyphs)            │   │
│  │  - Text wrapping + visible overflow markers             │   │
│  │  - Weather as TEXT in the footer row                     │   │
│  │  - Single light/landscape presentation (296×128)        │   │
│  │  - Host harness (CanvasTarget) + device (SeeedTarget)   │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                             │                                   │
│                             ▼                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Display Layer (Phase 1)                                │   │
│  │  epaper.update() → wait BUSY → ENABLE low → deep sleep  │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Refresh / Power Sequence

```
Boot → FirstBoot/Setup → Sync → Render → Sleep → (wake on button) → Sync → Render → Sleep
```

### Detailed flow:

1. **Boot** (`setup()`):
   - Initialize serial, display, NVS
   - If first boot or factory reset → enter setup wizard (captive portal)
   - Otherwise → load the stored configuration from NVS, which the setup portal writes.
     There is no compile-time credential path and no `secrets.h`

2. **Sync**:
   - Connect to Wi-Fi
   - Bounded NTP sync (6000 ms timeout) to retrieve local wall time
   - Evaluate time-based policies:
     - 00:00–14:59: Verse of the Day (BibleGateway VOTD)
     - 15:00–23:59: Word of the Day (A.Word.A.Day) — the window opens at 15:00, after the
       source's publish instant (00:01 US Eastern = 14:01 AEST / 15:01 AEDT), so the midday
       refresh no longer reads the previous edition (LESSONS §67). The page's own edition
       date is STILL compared with the device's local date as a backstop, because the flip
       lands at 16:01 local in the AEDT-with-US-standard-time months: a word that is not yet
       today's renders the verse instead of repeating yesterday's (LESSONS §70)
     - 00:00–17:59: Today's expected maximum + condition (footer row, unlabelled)
     - 18:00–23:59: Tomorrow's expected maximum + condition (footer row, prefixed `TOMORROW`)
   - Fetch content and weather over CA-validated HTTPS on a dedicated 16 KB FreeRTOS task (`cc_sync`)
   - Parse JSON/HTML into `VerseData` / `WordData` / `WeatherData` structs. The
     Word-of-the-Day fields are bounded by `WORD_FIELD_MAX` (1024) because the usage
     example is a whole paragraph; the body is composed by `cc_composeWordBody` so the
     host preview renders exactly the device's string (LESSONS §66)

3. **Render**:
   - Assemble `LayoutOptions` (header title, weather label, pronunciation caption)
   - Call `drawLayout(v, w, opts)` → routes to Seeed GFX backend (`SeeedTarget`)
   - Call `epaper.update()` (full ~25 s pigment sweep)
   - Call `epaper.sleep()` (power off panel driver)

4. **Sleep**:
   - Compute seconds until next scheduled slot via `cc_secondsUntilNextWake(now)`
   - Arm RTC timer wakeup
   - Arm active-low `ext1` wakeup mask on verified buttons (GPIO2, GPIO3, GPIO8)
   - Enter `esp_deep_sleep_start()` (~10 µA current draw)

5. **Button wake**:
   - Pressing any of the 3 user buttons wakes the chip (`wake_cause == ESP_SLEEP_WAKEUP_EXT1`)
   - Executes the exact same Sync → Render → Sleep sequence

---

## System Behaviour Specification: Weather, Schedule, Content & Failure Modes

### 1. Refresh Frequency & Timing (How often)
- **Scheduled RTC Timer Slots**: The device wakes at three fixed local times daily:
  - **06:00** (Morning wake — Verse of the Day)
  - **12:30** (Midday update — Verse of the Day, since the word window opens at 15:00)
  - **18:00** (Evening forecast — Word of the Day + tomorrow's outlook)
  The arithmetic (`cc_secondsUntilNextWake` in `sched/wake_schedule.cpp`) rolls over midnight and enforces a strict > 0 progression.
- **On-Demand User Button Wake**: Any press of BUTTON1 (GPIO2), BUTTON2 (GPIO3), or BUTTON3 (GPIO8) immediately wakes the chip from deep sleep and runs a full sync cycle.
- **Wake Storm Protection**: If 5 consecutive ext1 button wakes occur without an intervening timer wake or power cycle (`g_ext1Streak >= 5`), button wake is suppressed for one sleep interval to prevent battery exhaustion from stuck/noisy lines.
- **Clock Failure Retry**: If NTP fails to acquire local time, the device sleeps for a fallback interval of **1 hour** (`kFallbackSleepSec = 3600s`, clamped to a minimum guard of 60s) before retrying.

### 2. Weather Data Source & Transport (Where from)
- **API Provider**: [Open-Meteo](https://open-meteo.com/) REST API.
- **Query URL** — note `timezone=auto`, NOT the configured zone (see below):
  ```text
  https://api.open-meteo.com/v1/forecast?latitude=<LAT>&longitude=<LON>&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=2
  ```
- **Location**: `cc_configActive().latitude/longitude` — NVS when provisioned, otherwise the
  built-in defaults (lat `-37.8528`, lon `145.1633`, Burwood East, Victoria). Nothing is compiled
  in but those defaults; credentials are never compiled in at all.
- **Timezone**: stored as an **IANA name** (`Australia/Melbourne`); the POSIX rule newlib needs is
  derived from it in `config/tz_map.cpp`. The weather URL deliberately does *not* send it: Open-Meteo
  answers a POSIX string with HTTP 400, so the request sends `timezone=auto` and the API derives the
  zone from the coordinates.
- **TLS Security**: a bundle of **root** trust anchors — ISRG Root X1, ISRG Root X2 and Amazon
  Root CA 1 (`ROOT_CA_BUNDLE`) — applied to every request, so a server can rotate its
  intermediate certificate without breaking the device. Calls never use `setInsecure()`.
- **Stack Allocation**: Executed on a dedicated FreeRTOS task (`cc_sync`) with an explicit **16 KB stack** pinned to core 1, bypassing the fixed 8 KB Arduino loopTask limitation.

### 3. Data Selection & Mapping (What data)
The weather subsystem populates `WeatherData` (`temp`, `condition`, `alert`) and
`LayoutOptions::weatherLabel`. Note the layout draws only `temp` and `condition`: the ICON is no
longer rendered anywhere (the condition words carry more), though the field and the drawing
primitive remain — see LESSONS §53:
- **00:00–17:59 (Daytime outlook)**:
  - Temperature: Today's expected maximum (`daily.temperature_2m_max[0]`).
  - Condition: Today's expected forecast condition (`daily.weather_code[0]`).
  - Label: none (the temperature is understood as today's).
- **18:00–23:59 (Evening / next day outlook)**:
  - Temperature: Tomorrow's expected maximum (`daily.temperature_2m_max[1]`).
  - Condition: Tomorrow's expected forecast condition (`daily.weather_code[1]`).
  - Label: `TOMORROW`, drawn in red immediately before the temperature.
- **WMO Code Mapping (`cc_wmoCondition`)**:
  - `0` → "Clear"
  - `1, 2, 3` → "Partly cloudy"
  - `45, 48` → "Fog"
  - `51, 53, 55, 56, 57` → "Drizzle"
  - `61, 63, 65, 66, 67, 80, 81, 82` → "Rain"
  - `71, 73, 75, 77, 85, 86` → "Snow"
  - `95, 96, 99` → "Thunderstorm" (triggers alert)
  - All other codes → "Cloudy"
- **No icons**: the display is text-only. A 4-icon vector engine was removed on 2026-09-19 — the condition words carry more than a 10px glyph that only distinguished four states (LESSONS §61). `WeatherData` is `temp` + `condition` + `alert`.
- **Alert Text**: WMO codes 95, 96, 99 produce `"Severe weather warning"`.

### 4. Failure Modes & Degradation Hierarchy (Failure behaviour)
The device prioritizes maintaining a coherent ePaper image with visible diagnostics rather than failing silently or hanging:
- **No invented data (deliberate).** A failed fetch NEVER substitutes canned content or a
  defaulted reading. There is no bundled verse/word fallback: the body becomes an explicit
  "unavailable" screen and the footer row carries the reason as a red `OFFLINE:` /
  `PARTIAL:` alert. An earlier build showed a canned verse and a defaulted `0°C`, so a broken
  device looked like a working one.
- **Wi-Fi Connection Failure (`cc_wifiConnect() != 0`, or no SSID stored)**:
  - Sets `g_offlineReason = "no wifi"` and marks weather **invalid** (`WeatherData::valid = false`),
    so nothing renders a fabricated temperature.
  - Skips all fetches; the body becomes "No network connection. Check the Wi-Fi details, or move
    the device closer to the router."
  - The footer row displays the red warning `OFFLINE: no wifi` at the left.
- **NTP Time Sync Failure (`getLocalTime` timeout, 6 s)**:
  - `g_haveTime` stays false and the header date is blank.
  - Content mode falls back to the **verse** (the time-of-day rule needs a clock); the outlook
    falls back to today (no `TOMORROW` marker).
  - TLS **will** fail: the clock is still 1970, before every certificate's `notBefore`. The boot
    log says so explicitly, and a failed fetch reports
    `PARTIAL: <api> failed (device clock not set)` instead of implying the servers are down.
  - Sleep falls back to 1 hour (`kFallbackSleepSec`) rather than an invalid slot rollover.
- **Single API / TLS / parse failure while online** (`cc_fetchWeather`/`cc_fetchVerse`/`cc_fetchWord`
  returns false):
  - Sets `g_partialReason` (first failure wins); weather is marked invalid when it is the weather.
  - Body becomes the "Today's content could not be fetched..." screen; the footer row shows
    `PARTIAL: <api> failed` at the left, truncated with `...` if it cannot fit alongside the
    weather text.
  - Whatever genuinely succeeded still renders.
- **Network Disabled in Build (`#ifndef CHROMAWOTD_NETWORK`)**:
  - Sets `g_offlineReason = "no network in this build"`; renders the offline banner and the
    unavailable-content screen (there is no bundled fallback content).
- **Setup portal window expires (`cc_portalRun()` returns false)**:
  - The device deep-sleeps **without** running the sync/render cycle, so the setup screen (QR code
    + per-boot AP password) stays on the panel and the next wake re-opens the portal.
  - Previously it fell through into the sync path and repainted an OFFLINE error over the setup
    instructions (see LESSONS §46).
- **Footer row sharing (warning vs weather)**: both live on one row and neither may overprint
  the other. The weather half degrades first (drop the condition, then the `TOMORROW` label —
  the temperature always survives), and the warning is truncated with a visible `...` using the
  space that remains. Nothing is silently clipped.

---

## Security Model

### TLS / Certificate Validation (S1)

**All network I/O is HTTPS and must validate server certificates.**

- **Rule:** Never call `WiFiClientSecure::setInsecure()`. This disables certificate validation and is a MITM vulnerability.
- **Implementation:** Ship the relevant root CA(s) or a full CA bundle (e.g., Let's Encrypt, DigiCert) and call `client.setCACert(cert)` before every request.
- **Rationale:** The device fetches scripture + weather and later stores Wi-Fi credentials via a captive portal. An unauthenticated TLS connection is a real MITM surface (DNS/AP spoofing → injected verse, weather, or credential harvest through a fake portal).

### Credential Handling (S3)

**Credentials live on the device and are never echoed to serial.**

- **Resolution order** (implemented in `config/`): NVS → built-in defaults. One shared
  instance (`config_active.cpp`), so host and device cannot diverge — they did while
  each TU re-derived its own defaults.
- **There is no compile-time credential path.** Credentials are never read from a
  source file, so no build (local or CI) can produce an image containing them. An
  earlier design supported a gitignored `secrets.h` that did bake them into the
  binary; that file and its template were removed deliberately.
- **Storage:** `Preferences` / NVS namespace `chromawotd`, one key per field:
  - `ssid` (string), `pass` (string), `tz` (POSIX string), `lat` / `lon` (float)
  - `host` (string, optional device name), `mode` (content mode), `cfgver` (the
    firmware version that wrote the values), `cfgok` (provisioned marker)
  - Cached content (last verse/word/weather) is still **not implemented**.
- **Input:** the first-boot captive portal (`net/portal.cpp`): SoftAP named
  `ChromaWOTD-<MAC>` + DNS catch-all + a form on `192.168.4.1`. The AP password is
  regenerated per boot and shown on the ePaper; it is never persisted, so a
  captured password is useless once the device leaves setup mode.
- **Factory reset:** hold any button ≥10 s through a button wake — the gesture is classified
  by length in `sched/hold_gesture.{h,cpp}` and the sampling lives in `main.cpp` (it must use the
  RTC domain). It erases the namespace via `cc_configEraseNvs()` in `config/config_nvs.cpp` and
  returns to the portal.
- **Output:** never logged. The firmware reports only `(set)` / `(empty)`, and
  `Serial.printf` must not be given a credential field.
- **Two different "timezone" needs, deliberately separated:** the local clock uses a
  POSIX TZ string, while the weather API is asked for `timezone=auto` because
  Open-Meteo rejects a POSIX string with HTTP 400 (that conflation silently broke
  every weather fetch — see `cc_fetchWeather`).

### Remote String Safety (S4)

**Remote text is untrusted input. Fixed buffers in the renderer are the boundary.**

- **Rule:** Never `sprintf` remote text. Only `snprintf` / bounded-copy.
- **Buffers:** `wordBuf[64]`, `lineBuf[96]` in `verse_display.cpp` are the boundary. Add comments at these locations.
- **Parser:** Only parse `text` / `reference` from BibleGateway (HTML entities stripped). Ignore `content` (carries `<h3>`/`<span>`).

### OTA / Signed Updates (S5)

**Not needed for a personal ambient display, but if this ever becomes a shared/shipped device, OTA images are unsigned and the portal is a credential surface.**

- **Decision:** Document in the plan's release-tagging item so it's a conscious decision, not an oversight.

---

## Application State Machine (D7)

**States:** `Boot` → `FirstBoot/Setup` → `Sync` → `Render` → `Sleep` → `Error/Offline` → (retry) → `Sync`

### State transitions:

| From | To | Trigger |
|------|----|---------|
| `Boot` | `FirstBoot/Setup` | First boot, or factory reset (NVS wipe) |
| `FirstBoot/Setup` | `Sync` | User completes captive portal wizard |
| `Sync` | `Render` | Network fetch completes (success or cached fallback) |
| `Render` | `Sleep` | `epaper.sleep()` + `ESP.deepSleep()` |
| `Sleep` | `Sync` | Button wake (ext1 mask) |
| `Sync` | `Error/Offline` | Network timeout / API error |
| `Error/Offline` | `Sync` | Retry timer expires (exponential backoff) |

### Implementation:
- Document in `docs/ARCHITECTURE.md` **before** implementing Phase 2.
- Buttons, NVS, deep sleep, and the offline banner have a home rather than being bolted onto `loop()`.

---

## Module Map

Implemented 2026-09-12 (REVIEW D1), extended as the config/network/scheduling layers landed:

```
firmware/src/
  verse_display.h           data structs + enums (VerseData, WeatherData, Theme, Orientation)
                            — the public contract, and the layout's geometry constants
  verse_display.cpp         font metrics + the layout view functions (header band, verse block, footer row)
  main.cpp                  the cycle: wake → config → portal → sync → render → sleep,
                            plus the wake-gesture classification
  text/                     PURE: no backend, no flash — unit-tested directly
    glyphs.{h,cpp}          cc_utf8ToAscii / cc_utf8ToAsciiN — one decoder, length-bounded
    wrap.{h,cpp}            cc_lineCapacity, cc_lineBudget — pure line/budget math
    date_format.{h,cpp}     cc_formatHeaderDate — "DOW DD MMM", shared by the device and the renders
  sched/                    PURE policy: decisions, no I/O
    wake_schedule.{h,cpp}   06:00 / 12:30 / 18:00 next-slot arithmetic
    content_policy.{h,cpp}  verse-vs-word by hour, the portal's forced mode, the gesture invert
    hold_gesture.{h,cpp}    tap / short hold / long hold classification + the session driver
  config/                   DeviceConfig: one struct, one resolution rule
    config.{h,cpp}          validation, coordinate parsing, bounded copy (host + device)
    config_active.{h,cpp}   the ONE resolved instance (NVS → built-in defaults)
    config_nvs.{h,cpp}      Preferences persistence (device) / in-memory stub (host)
    config_compiletime.h    the built-in defaults — and no credential path, ever
    tz_map.{h,cpp}          IANA name → POSIX rule
  net/
    net.{h,cpp}             shared parse/mapping (WMO codes, HTML entities) + host curl fetch
    net_host_curl.cpp       the host fetch hook (device fetch is next door)
    net_impl_esp32.{h,cpp}  the device fetch: Wi-Fi, CA-validated TLS, ArduinoJson
    portal.{h,cpp}          first-boot captive-portal wizard
    wifi_qr.{h,cpp}         the Wi-Fi QR payload
  draw/
    target.h                DisplayTarget interface
    target_seeed.cpp        Seeed GFX backend (colour mapping, proportional glyph draw)
    target_canvas.cpp       host canvas backend (mock + PNG)
    font_types.h            GFXglyph/GFXfont/PROGMEM — host shim or device gfxfont.h
  fonts/                    generated GFX font tables (tools/font_convert.py)
  third_party/qrcodegen/    vendored QR encoder (Nayuki, MIT)
```

**Rationale:** `verse_display.cpp` was split along the seams identified in REVIEW D1.
The layout functions are a small "view" layer on top of stable primitives; the UTF-8
decoder and wrap math are pure, unit-testable units (see `test_text.cpp`); the backend
is behind the `DisplayTarget` interface so adding a third target means implementing
one interface, not copying a `#ifdef` block. The mock canvas lives only under
`firmware/test/` and carries no product logic.

---

## Build & Test

### Firmware (PlatformIO):
```bash
cd firmware
pio run -e s3          # build
pio run -e s3 -t upload  # flash
pio device monitor -b 115200  # serial monitor
```

### Host tests (CMake + GoogleTest):
```bash
cmake -S firmware/test -B firmware/test/build -G Ninja
cmake --build firmware/test/build
ctest --test-dir firmware/test/build --output-on-failure
```

### One-command verification:
```bash
python tools/verify_all.py              # build + tests + render ledger
python tools/verify_all.py --clean      # full rebuild first
python tools/verify_all.py --skip-firmware   # fast host-only loop
python tools/verify_all.py --fix        # resync docs/images after intentional change
```

### Render ledger:
- `firmware/test/output/*.png` (gitignored, staging)
- `docs/images/*.png` (committed, byte-identical to ctest output)
- `tools/verify_all.py` md5-compares the two; reports missing, stale, or orphan files.

---

## Key Lessons

1. **Exact-Power Display Sequencing:** On JD79661 panels, drawing must be completely staged before `update()`. Powering down the panel while busy will cause panel latch-up.
2. **Flat Library Includes in PlatformIO:** Seeed_GFX root `TFT_eSPI.cpp` includes subfolder source files internally, and PlatformIO compiles only that root directory, so `main.cpp` includes `TFT_eSPI.cpp` directly. There is no subfolder-exclusion option to configure — `lib_build_src_filter` is not a PlatformIO setting and is ignored with a warning.
3. **Pure View Decoupling:** Isolate rendering math from network/NVS state by passing pure data structs by value; split pure text/decoder math into its own module so it is directly unit-testable.
4. **No Silent Truncation:** Any content block that can overflow must mark the cut (`...`) rather than dropping words; region invariants in `test_layout_overflow` enforce that nothing leaves its block.
5. **Dual-Target Discipline:** Host renders must byte-match the device path. The PNG archive + `verify_all.py` ledger make this enforceable.

---

## Open Questions

- **Font:** The verse body auto-sizes through a Roboto ladder at **10 / 9 / 8 / 7 / 6 / 5.5 / 5pt**
  (mono-hinted, `-DCHROMAWOTD_FONT_FREESANS`), picking the largest whose wrapped line count fits
  the block. Short content gets 10pt; the longest realistic verse lands on 5.5pt. There is also a
  dedicated 10pt temperature font. Future work may explore other densities or vertical centring
  for short content, which still leaves the lower block empty at the top rung.
- **State machine:** Documented here, but implementation deferred to Phase 2 (REVIEW D7).

---

## References

These are relative to THIS file (in `docs/`) — the paths were previously written as
`docs/X`, which resolves to `docs/docs/X` and 404s on GitHub.

- [REVIEW.md](REVIEW.md) — Critical code & project review (security, design, hygiene)
- [CODE_REVIEW.md](CODE_REVIEW.md) — Phases 2–5 review, with its fix-status table
- [UI_HISTORY.md](UI_HISTORY.md) — how the display has changed, era by era, with the renders
- [LESSONS_LEARNT.md](lessons/LESSONS_LEARNT.md) — Hard-won findings, hardware quirks, and solutions
- [PROJECT_PLAN.md](PROJECT_PLAN.md) — Phased implementation plan and milestones
- [STATUS.md](STATUS.md) — Current project phase, completed tasks, and next steps
