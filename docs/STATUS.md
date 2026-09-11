# CHROMAWOTD — Status

**Updated:** 2026-09-12
**Phase:** 1 — Display bring-up & Layout Prototype
**Firmware version:** 0.1.0

| Item | State |
|---|---|
| Repo scaffold | ✅ done |
| Firmware builds | ✅ `pio run -e s3` SUCCESS with Seeed GFX (BWRY panel, combo 512) |
| Display bring-up | ✅ 4-colour smoke pattern verified & flashed |
| Dual-target layout engine | ✅ `verse_display.cpp` runs on target (Seeed GFX) & host (CMake test harness) |
| Layout orientations & themes | ✅ Portrait, Landscape, Inverted, and Midnight Dark renderers implemented |
| Forecast header & text wrapping | ✅ Centred "FORECAST" section header with underline divider & wrapped condition/alert text |
| Host layout tests & PNG exports | ✅ Portrait, Landscape, Alert, and Inverted test suites passing (archived to `docs/images/`) |
| Degree symbol fix | ✅ Decoded `0xC2 0xB0` to vector circle for crisp `27°C` render |
| Clock / weather fetch | ❌ Phase 2–3 |
| Dual buttons & WOTD toggle | ❌ Phase 2–3 (Mode switch + Refresh/Content toggle) |

## Next steps

1. **Dual Button Controls & Deep Sleep Wakeup**:
   - **Mode Button**: Toggle between visual modes (Light vs Inverted/Dark, Portrait vs Landscape).
   - **Refresh & Content Toggle Button**: Wake up to refresh weather and switch between **Verse of the Day** (Scripture) and **Word of the Day** (Vocabulary).
   - Non-volatile memory (`Preferences` / NVS) to store user's selected mode across sleep intervals.
2. Wire WiFi NTP time synchronization and timezone handling.
3. Implement Open-Meteo weather fetch and ArduinoJson parsing.
4. Implement dual content fetch: Scripture Verse + Vocabulary Word (Wordnik / Merriam-Webster).


