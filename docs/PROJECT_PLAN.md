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
- [x] **First-Boot Setup Screen** (2026-09-13): the ePaper shows the AP name, the
      per-boot portal password and the steps (`http://192.168.4.1`).
- [x] **Captive Portal Wi-Fi Wizard** (2026-09-13): SoftAP + DNS catch-all + a form
      for SSID/passphrase, timezone, coordinates, device name and content mode;
      values validated and written to NVS (`config/`, `net/portal.cpp`).
- [x] **Factory Reset** (2026-09-13): 10 s button hold through a button wake wipes
      the NVS namespace and returns to the setup portal (`sched/factory_reset.cpp`).
- [x] **Button Hardware Interaction & Deep Sleep Wake** (Phase 5, 2026-09-12):
  - [x] `ext1` multi-button deep sleep wake mask — **BUTTON1/2/3 = GPIO2/GPIO3/GPIO8
        (D1/D2/D9)**, active-low, all RTC-capable. **D0/GPIO1 is `BAT_ADC`**, not a button.
        The pin map was *probed on hardware* (`env:probe`); the schematic-derived
        D1/D2/D4 guess was wrong (D4 is `I2C_SDA`) and caused a deep-sleep wake storm —
        see LESSONS §34.
  - [x] All three buttons run a full immediate re-sync + refresh (same path as a timer
        wake). Verified on hardware: `wake cause: 3 (button)` → sync → refresh → sleep.
  - [x] Content mode is time-based, not toggled: Verse 00:00–11:59, Word 12:00–23:59.
  - [ ] Lockout during active ~25s screen sweep to ignore switch bounce/spam.
- [ ] **Non-Volatile State Persistence (`Preferences` / NVS)**:
  - [x] Wi-Fi credentials, timezone IANA name, latitude/longitude (2026-09-13).
  - [x] Resolution order NVS → built-in default (one shared instance); no compile-time credential path.
  - [ ] Active display layout & content mode.
  - [ ] Cached last successful verse, word, and weather data with timestamp.

#### ⚠️ FIX NEEDED — stored config can be silently wiped (LESSONS §45)

**Observed on hardware (2026-09-13):** a provisioned device (it had joined the home
network and rendered real content) later booted reporting every key
`nvs_get_blob len fail: NOT_FOUND` and re-raised the setup portal. The stored
credentials were destroyed — with nothing in the UI to explain it.

**Cause (diagnosed, see §45):** the Arduino core's `initArduino()` calls
`nvs_flash_init()` and, on `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND`,
**erases and reformats the entire NVS partition**:

```c
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_partition_erase_range(partition, 0, partition->size);   // wipes EVERYTHING
    err = nvs_flash_init();
}
```

Our partition is only 20 KB and is **shared with the WiFi/BLE/DHCP stack**
(`misc`, `nvs.net80211`, `phy`, `dhcp_state`). When it fills, everything goes.
Not our code: `cc_configEraseNvs()` is reachable only from the 10 s reset gesture.

**Evidence.** Offline dump of the region: namespace `chromawotd` at nsIndex=4 with all
16 entries well formed, but the page sequence numbers start at 0 and increment — the
signature of a fresh reformat. Not yet proven that `NO_FREE_PAGES` was the specific
trigger (the core's failure line only logs if the *re*-init fails, so a successful
reformat is silent).

**Fix implemented 2026-09-19 — awaiting bench verification (LESSONS §48).**

1. **Our config now has its own NVS partition — DONE.** `firmware/partitions.csv` adds a
   dedicated 16 KB `nvs_cfg` (`data, nvs`) entry, `platformio.ini` sets
   `board_build.partitions`, and `config_nvs.cpp` passes the label to all three
   `Preferences::begin()` calls.
   *Baseline correction:* the table to copy is the framework's **`default_8MB.csv`**, not
   `default.csv` — the board definition sets `"partitions": "default_8MB.csv"`, whose
   app0 size of `0x330000` is the 3342336-byte slot PlatformIO reports. `default.csv` has
   a `0x140000` app0 and different `app1`/`spiffs` offsets, so copying it would lay out
   the flash wrongly.
   *Ordering is load-bearing:* `nvs` must stay the FIRST `data, nvs` entry, because the
   core locates its erase target with `esp_partition_find_first(DATA, NVS, NULL)` — a
   NULL label, i.e. the first match by subtype. Put `nvs_cfg` above `nvs` and the core
   would erase `nvs_cfg` instead.
   *Offsets are unchanged* for `nvs`/`otadata`/`app0`, so `tools/merge_firmware.py` needs
   no change and an app-only flash at `0x10000` still works — but the table itself
   (`partitions.bin` @ `0x8000`) must be re-flashed once, alongside the app.
   `spiffs` and `app1` are both unused by this project, which is where the space comes
   from. Existing devices DO lose their stored config once (our namespace moves to a new,
   empty partition), so the setup portal has to be re-run; the old values remain in the
   old partition, so re-flashing the old table restores them if a rollback is ever needed.
   `Preferences::begin()` calls `nvs_flash_init_partition(label)` itself, so no separate
   init call is required.
2. **Read-back verification after save.** `cc_configSaveToNvs()` should load the values
   back and compare before the portal reports success; a silent write failure is
   currently indistinguishable from success. *(Still open.)*
3. **Warn on unexpected loss.** If the device has ever been provisioned (a flag in a
   separate namespace, or an RTC/marker value) and now finds nothing, say so on the panel
   instead of quietly showing the setup portal. *(Still open.)*
4. **Re-test — tooling in place, bench run outstanding.** Two envs reproduce the failure
   deterministically instead of relying on filling WiFi NVS by hand over many
   connect/disconnect cycles: `env:nvsprobe_legacy` (old table — config shares `nvs`) and
   `env:nvsprobe` (new table — config in `nvs_cfg`). Each fills the shared partition until
   writes fail, reboots so the core's `nvs_flash_init()` sees a full partition, then
   reports whether the config survived. Run as a pair to *demonstrate* the fix.

### Phase 3 — Weather, Dual Content Sources & Sync Indicators
- [ ] **Syncing & Status Feedback**:
  - Hardware user LED (`LED_BUILTIN` / GPIO 21) active pulse during Wi-Fi connection and API fetch (avoiding unneeded 25s screen updates).
  - Diagnostic LED blink cadences on connection failure.
  - [x] Graceful failure state: if Wi-Fi / API times out, render fallback content with a
        prominent red banner (`OFFLINE: [Reason]`).
  - Exponential / 15-minute retry backoff on network failure before re-entering sleep.
- [x] **NTP Synchronization**: SNTP sync on wake, POSIX timezone adjustment with automatic DST handling.
- [x] **Weather Pipeline**: Open-Meteo REST daily forecast fetch, ArduinoJson parsing, weather code to BWRY icon mapping, alert detection. Daytime (< 18:00) pulls today's expected maximum + condition; evening (≥ 18:00) pulls tomorrow's expected maximum + condition.
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
