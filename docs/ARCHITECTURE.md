# CHROMAWOTD — Architecture

**Last updated:** 2026-09-12
**Firmware version:** 0.1.0
**Phase:** 1 — Display bring-up & Layout Prototype (complete)

---

## Overview

CHROMAWOTD is a 2.9" quad-colour ePaper display (BWRY: black, white, red, yellow) showing:
- **Verse of the Day** (scripture) or **Word of the Day** (vocabulary) — toggled via button
- **Local weather** (temperature, condition, icon, alerts)
- **Date** in the header

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
│  │  WeatherData { temp, condition, alert, icon }           │   │
│  │  Cached last successful fetch + timestamp               │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                             │                                   │
│                             ▼                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Layout Engine (Phase 1, dual-target)                   │   │
│  │  verse_display.cpp + text/ + draw/ modules              │   │
│  │  - UTF-8 → ASCII normalisation (text/glyphs)            │   │
│  │  - Text wrapping + visible overflow markers             │   │
│  │  - Weather icon vector drawing (draw/weather_icon)      │   │
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
   - Otherwise → load cached credentials from NVS / secrets

2. **Sync**:
   - Connect to Wi-Fi
   - Bounded NTP sync (6000 ms timeout) to retrieve local wall time
   - Evaluate time-based policies:
     - 00:00–11:59: Verse of the Day (BibleGateway VOTD)
     - 12:00–23:59: Word of the Day (A.Word.A.Day)
     - 00:00–17:59: Today's expected maximum + condition (`FORECAST`)
     - 18:00–23:59: Tomorrow's expected maximum + condition (`TOMORROW`)
   - Fetch content and weather over CA-validated HTTPS on a dedicated 16 KB FreeRTOS task (`cc_sync`)
   - Parse JSON/HTML into `VerseData` and `WeatherData` structs

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
  - **06:30** (Morning wake)
  - **12:30** (Midday update)
  - **18:00** (Evening forecast)
  The arithmetic (`cc_secondsUntilNextWake` in `sched/wake_schedule.cpp`) rolls over midnight and enforces a strict > 0 progression.
- **On-Demand User Button Wake**: Any press of BUTTON1 (GPIO2), BUTTON2 (GPIO3), or BUTTON3 (GPIO8) immediately wakes the chip from deep sleep and runs a full sync cycle.
- **Wake Storm Protection**: If 5 consecutive ext1 button wakes occur without an intervening timer wake or power cycle (`g_ext1Streak >= 5`), button wake is suppressed for one sleep interval to prevent battery exhaustion from stuck/noisy lines.
- **Clock Failure Retry**: If NTP fails to acquire local time, the device sleeps for a fallback interval of **1 hour** (`kFallbackSleepSec = 3600s`, clamped to a minimum guard of 60s) before retrying.

### 2. Weather Data Source & Transport (Where from)
- **API Provider**: [Open-Meteo](https://open-meteo.com/) REST API.
- **Query URL**:
  ```text
  https://api.open-meteo.com/v1/forecast?latitude=<LAT>&longitude=<LON>&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=<TZ>&forecast_days=2
  ```
- **Default Location**: Lat `-37.8528`, Lon `145.1633`, Timezone `"Australia/Melbourne"` (Burwood East, Victoria). Overridable per-build or in `secrets.h`.
- **TLS Security**: Pinned Let's Encrypt **YR2** intermediate CA (`ROOT_CA_YR2`) chaining to ISRG Root YR. Calls never use `setInsecure()`.
- **Stack Allocation**: Executed on a dedicated FreeRTOS task (`cc_sync`) with an explicit **16 KB stack** pinned to core 1, bypassing the fixed 8 KB Arduino loopTask limitation.

### 3. Data Selection & Mapping (What data)
The weather subsystem populates `WeatherData` (`temp`, `condition`, `alert`, `icon`) and `LayoutOptions::weatherLabel`:
- **00:00–17:59 (Daytime outlook)**:
  - Temperature: Today's expected maximum (`daily.temperature_2m_max[0]`).
  - Condition: Today's expected forecast condition (`daily.weather_code[0]`).
  - Label: `"FORECAST"`.
- **18:00–23:59 (Evening / next day outlook)**:
  - Temperature: Tomorrow's expected maximum (`daily.temperature_2m_max[1]`).
  - Condition: Tomorrow's expected forecast condition (`daily.weather_code[1]`).
  - Label: `"TOMORROW"`.
- **WMO Code Mapping (`cc_wmoCondition`)**:
  - `0` → "Clear"
  - `1, 2, 3` → "Partly cloudy"
  - `45, 48` → "Fog"
  - `51, 53, 55, 56, 57` → "Drizzle"
  - `61, 63, 65, 66, 67, 80, 81, 82` → "Rain"
  - `71, 73, 75, 77, 85, 86` → "Snow"
  - `95, 96, 99` → "Thunderstorm" (triggers alert)
  - All other codes → "Cloudy"
- **Vector Icons (`WeatherIcon`)**: Coarse 4-icon vector engine (`Sun` = 0, `Cloud` = 1, `Rain` = 2, `PartlyCloudy` = 3). WMO 0 maps to Sun; 1–3 to PartlyCloudy; ≥80 to Rain; remainder to Cloud.
- **Alert Text**: WMO codes 95, 96, 99 produce `"Severe weather warning"`.

### 4. Failure Modes & Degradation Hierarchy (Failure behaviour)
The device prioritizes maintaining a coherent ePaper image with visible diagnostics rather than failing silently or hanging:
- **Wi-Fi Connection Failure (`cc_wifiConnect() != 0`)**:
  - Sets `g_offlineReason = "no wifi"`.
  - Skips network fetches. Uses fallback scripture text and default weather (`temp = 0.0f`, `icon = PartlyCloudy`, condition `"Temp 0"`).
  - Weather column displays red alert banner: `OFFLINE: no wifi`.
- **NTP Time Sync Failure (`getLocalTime` timeout)**:
  - Header date displays `"Offline"`.
  - Content mode defaults to Verse of the Day; weather outlook defaults to daytime (`g_tomorrow = false`, `"FORECAST"`).
  - Sleep duration falls back to 1 hour (`kFallbackSleepSec`) instead of an invalid slot rollover.
- **Weather API / TLS / JSON Parse Failure (`cc_fetchWeather` returns false)**:
  - Leaves existing `WeatherData` untouched (or initializes to defaults).
  - Sets `g_offlineReason = "weather API"`.
  - Condition synthesizes `"Temp 0"` if empty.
  - Weather column displays red alert banner: `OFFLINE: weather API`.
  - Verse or Word content still renders normally if its separate fetch succeeded.
- **Network Disabled in Build (`#ifndef CHROMAWOTD_NETWORK`)**:
  - Sets `g_offlineReason = "network disabled in build"`.
  - Renders offline banner and bundled fallback content.
- **Visual Column Reflow on Alert vs Normal**:
  - **With Alert or Offline Banner**: Weather icon drops to minimum size (`kIconMin = 24px`) pinned at top; red divider and red `ALERT:` label are drawn; alert text wraps at bottom in compact 5pt font.
  - **Without Alert**: Icon dynamically expands (up to `kIconMax = 48px`) to fill the vertical whitespace in the weather column.

---

## Security Model

### TLS / Certificate Validation (S1)

**All network I/O is HTTPS and must validate server certificates.**

- **Rule:** Never call `WiFiClientSecure::setInsecure()`. This disables certificate validation and is a MITM vulnerability.
- **Implementation:** Ship the relevant root CA(s) or a full CA bundle (e.g., Let's Encrypt, DigiCert) and call `client.setCACert(cert)` before every request.
- **Rationale:** The device fetches scripture + weather and later stores Wi-Fi credentials via a captive portal. An unauthenticated TLS connection is a real MITM surface (DNS/AP spoofing → injected verse, weather, or credential harvest through a fake portal).

### Credential Handling (S3)

**Wi-Fi credentials, timezone, and location live in NVS only. They are never echoed to serial.**

- **Storage:** `Preferences` / NVS namespace `chromawotd`:
  - `wifi_ssid` (string)
  - `wifi_passphrase` (string)
  - `timezone` (POSIX string, e.g., `Australia/Sydney`)
  - `lat` / `lon` (float)
  - `content_mode` (enum: 0=verse, 1=word)
  - `last_verse` / `last_weather` (cached JSON, optional)
- **Input:** Entered over the captive portal (Phase 2+). Portal AP password is per-boot random.
- **Output:** Never logged to `Serial`. `Serial.printf` must sanitize or skip credential fields.

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

Implemented 2026-09-12 (REVIEW D1):

```
firmware/src/
  verse_display.h           data structs + enums (VerseData, WeatherData, WeatherIcon, Theme, Orientation)
  verse_display.cpp         font metrics + the layout view functions (header, verse block, weather column)
  text/
    glyphs.{h,cpp}          cc_utf8ToAscii / cc_utf8ToAsciiN — one decoder, length-bounded
    wrap.{h,cpp}            cc_lineCapacity, cc_lineBudget — pure line/budget math
  draw/
    weather_icon.{h,cpp}    drawWeatherIcon (vector icons, draws through DisplayTarget)
    font_types.h            GFXglyph/GFXfont/PROGMEM — host shim or device gfxfont.h
    target.h                DisplayTarget interface
    target_seeed.cpp        Seeed GFX backend (colour mapping, FreeSans glyph draw)
    target_canvas.cpp       host canvas backend (mock + PNG)
  main.cpp                  application state machine (Phase 2+)
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

- **Font:** The verse body uses Roboto 5.5pt (mono-hinted, `-DCHROMAWOTD_FONT_FREESANS`),
  with 5pt and 6pt candidates for auto-sizing and a dedicated 10pt temperature font.
  Future work may explore other densities for different content layouts.
- **State machine:** Documented here, but implementation deferred to Phase 2 (REVIEW D7).

---

## References

- [REVIEW.md](docs/REVIEW.md) — Critical code & project review (security, design, hygiene)
- [LESSONS_LEARNT.md](docs/lessons/LESSONS_LEARNT.md) — Hard-won findings, hardware quirks, and solutions
- [PROJECT_PLAN.md](docs/PROJECT_PLAN.md) — Phased implementation plan and milestones
- [STATUS.md](docs/STATUS.md) — Current project phase, completed tasks, and next steps
