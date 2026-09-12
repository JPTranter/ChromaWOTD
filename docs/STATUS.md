# CHROMAWOTD — Status

**Updated:** 2026-09-12
**Phase:** 1 — Display bring-up & Layout Prototype (complete)
**Firmware version:** 0.1.0

| Item | State |
|---|---|
| Repo scaffold | ✅ done |
| Firmware builds | ✅ `pio run -e s3` SUCCESS from a clean tree (Flash 9.6%, RAM 5.8%); only library-side warnings |
| Display bring-up | ✅ 4-colour smoke pattern verified & flashed (hardware state as of this session's earlier work) |
| Dual-target layout engine | ✅ `verse_display.cpp` runs on target (Seeed GFX) & host (CMake test harness) |
| Layout orientations & themes | ✅ Single light landscape presentation only (portrait + inverted/dark dropped 2026-09-12) |
| Forecast header & text wrapping | ✅ Centred "FORECAST" section header with underline divider & wrapped condition/alert text |
| Host layout tests & PNG exports | ✅ 26 tests across 4 suites, all passing (`test_layout_landscape/_alert/_overflow/_text`) |
| Degree symbol fix | ✅ Decoded `0xC2 0xB0` to vector circle for crisp `27°C` render |
| UTF-8 → ASCII normalisation | ✅ Shared `cc_utf8ToAscii()` maps curly quotes, dashes, NBSP, ellipsis, `⚠`; host and device now agree |
| Overflow handling | ✅ Blocks mark swallowed text with an inline `...` marker; region invariants prove nothing spills (archived: `docs/images/layout_overflow_*.png`) |
| Temperature rounding | ✅ `lroundf()` — negative temps no longer round toward zero (`-0.6` → `-1`) |
| Highlight matching | ✅ Case-insensitive fallback + overlap-based word colouring; `verseHighlightFound()` surfaces misses |
| Build config hygiene | ✅ Inert `lib_build_src_filter` removed (PlatformIO warned on every build); `firmware/build_s3.log` untracked |
| Repeat-operation tooling | ✅ `tools/verify_all.py` (build + tests + render ledger, `--fix` to resync) and `tools/render_preview.py` (preview any fixture via `layout_render`, no flashing) |
| Clock / weather fetch | ✅ Phase 3 complete: **verified live on hardware** (XIAO ESP32-S3) — Wi-Fi connects, both Open-Meteo + BibleGateway fetches succeed over validated TLS, real content renders and refreshes with no crash |
| Wake schedule & deep sleep | ✅ Phase 4 (schedule): deep sleep between 06:30 / 12:30 / 18:00 slots, NTP re-sync each wake, 1 h fallback if the clock is invalid; entry verified on hardware (sleeps to next slot) |
| Dual buttons & WOTD toggle | ❌ Phase 2–3 (Mode switch + Refresh/Content toggle) |

## Next steps

> A full critical review of the code and project — security, design, readability and
> hygiene, with a priority order — is in [`docs/REVIEW.md`](REVIEW.md). The items below
> are the product road map; REVIEW.md is the quality backlog.

1. **Dual Button Controls & Deep Sleep Wakeup**:
   - Only the **Refresh & Content Toggle** button is needed — the display has a single
     light/landscape presentation, so there is no mode/theme button.
   - Refresh & Content Toggle wakes the device to refresh weather and switch between
     **Verse of the Day** (Scripture) and **Word of the Day** (Vocabulary).
   - Non-volatile memory (`Preferences` / NVS) to store the active content mode across sleep intervals.
2. Wire WiFi NTP time synchronization and timezone handling.
3. Implement Open-Meteo weather fetch and ArduinoJson parsing (endpoint verified live: 610 B payload for Burwood East).
4. Implement dual content fetch: Scripture Verse + Vocabulary Word (BibleGateway VOTD JSON verified live; Wordnik / Merriam-Webster still to evaluate).

## Layout follow-ups (from the 2026-09-12 review, not yet done)

- [x] ~~Portrait strip/verse-block issues~~ Resolved by dropping portrait mode entirely
  (single landscape, light presentation, 2026-09-12).
- `drawWrappedTextCentered`/`drawVerseBlock` geometry constants could still move into a
  block-descriptor type, but it is far lower priority with only one layout.
- `main.cpp` still ships a hardcoded fixture and never sleeps; both are Phase 2/4 work.
