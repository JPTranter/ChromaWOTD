# CHROMAWOTD — Status

**Updated:** 2026-09-13
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
| User buttons | ✅ Phase 5: BUTTON1/2/3 = **GPIO2/GPIO3/GPIO8** (D1/D2/D9), active-low. Any press wakes the device via `ext1` and runs a full sync+refresh — **verified on hardware** (`wake cause: 3 (button)` → sync → refresh → sleep). Pin map probed, not guessed: the schematic reading was wrong (see LESSONS §34) |
| Time-based content | ✅ Phase 5: Verse of the Day 00:00–11:59, Word of the Day 12:00–23:59 (header title switches); pure policy in `sched/content_policy` + tests |
| Word of the Day source | ✅ Phase 5: A.Word.A.Day (`wordsmith.org/words/today.html`) — definition + example body, respelling pronunciation caption, headword caption; bundled fallback word if the fetch fails |
| Weather policy & outlook | ✅ Daytime (< 18:00) shows today's expected maximum + condition under **FORECAST**; evening (18:00–23:59) shows tomorrow's expected maximum + condition under **TOMORROW** |
| Dual buttons & WOTD toggle | ✅ superseded — content is time-based, all three buttons sync (no manual toggle) |
| Weather icon mapping | ⚠️ **known limitation, accepted** — only WMO ≥ 80 get the Rain icon; Drizzle (51-57) and Rain (61-67) fall through to the plain Cloud icon. Text label is correct; decided 2026-09-12 to leave as-is (see LESSONS §35) |
| Verse font auto-size | ✅ Ladder (`Roboto 6pt → 5.5pt → 5pt`, largest that fits the block) extracted to `cc_pickVerseFont()`/`cc_verseFontSize()`; block geometry now lives once in `verse_display.h` (`kVerseMaxW`/`kVerseMaxH`). Live 2026-09-13 VOTD (Phil 4:4) verified selecting **6pt** (see LESSONS §38) |
| Auto-size local coverage | ✅ `test_verse_autosize` — CMake compiles this target **with** `-DCHROMAWOTD_FONT_FREESANS=1`, closing a blind spot where every other host target exercised only the non-FreeSans path. 7/7 suites pass; `verify_all.py` ALL GREEN |
| Device-font host suite | ✅ `-DCHROMAWOTD_DEVICE_FONTS=ON` runs the layout invariants on the shipped proportional font path; two font-calibrated tests were corrected (overflow-marker detector, alert-rule probe) after a render proved the product was fine and only the tests were. Wired in as `verify_all.py` stage 3/5 (see LESSONS §39) |
| Font decision tooling | ✅ `tools/font_size_probe.py --live` reports which body font today's content gets, straight from `cc_verseFontSize()` via `layout_render`; `tools/flash_when_awake.py` retry-loops an upload until the deep-asleep port returns |

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
