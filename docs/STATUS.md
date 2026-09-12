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
| Layout orientations & themes | ✅ Portrait, Landscape, Inverted, and Midnight Dark renderers implemented |
| Forecast header & text wrapping | ✅ Centred "FORECAST" section header with underline divider & wrapped condition/alert text |
| Host layout tests & PNG exports | ✅ 19 tests across 4 suites, all passing (`test_layout_portrait/_landscape/_alert/_overflow`) |
| Degree symbol fix | ✅ Decoded `0xC2 0xB0` to vector circle for crisp `27°C` render |
| UTF-8 → ASCII normalisation | ✅ Shared `cc_utf8ToAscii()` maps curly quotes, dashes, NBSP, ellipsis, `⚠`; host and device now agree |
| Overflow handling | ✅ Blocks mark swallowed text with an inline `...` marker; region invariants prove nothing spills (archived: `docs/images/layout_overflow_*.png`) |
| Temperature rounding | ✅ `lroundf()` — negative temps no longer round toward zero (`-0.6` → `-1`) |
| Highlight matching | ✅ Case-insensitive fallback + overlap-based word colouring; `verseHighlightFound()` surfaces misses |
| Build config hygiene | ✅ Inert `lib_build_src_filter` removed (PlatformIO warned on every build); `firmware/build_s3.log` untracked |
| Repeat-operation tooling | ✅ `tools/verify_all.py` (build + tests + render ledger, `--fix` to resync) and `tools/render_preview.py` (preview any fixture via `layout_render`, no flashing) |
| Clock / weather fetch | ❌ Phase 2–3 |
| Dual buttons & WOTD toggle | ❌ Phase 2–3 (Mode switch + Refresh/Content toggle) |

## Next steps

> A full critical review of the code and project — security, design, readability and
> hygiene, with a priority order — is in [`docs/REVIEW.md`](REVIEW.md). The items below
> are the product road map; REVIEW.md is the quality backlog.

1. **Dual Button Controls & Deep Sleep Wakeup**:
   - **Mode Button**: Toggle between visual modes (Light vs Inverted/Dark, Portrait vs Landscape).
   - **Refresh & Content Toggle Button**: Wake up to refresh weather and switch between **Verse of the Day** (Scripture) and **Word of the Day** (Vocabulary).
   - Non-volatile memory (`Preferences` / NVS) to store user's selected mode across sleep intervals.
2. Wire WiFi NTP time synchronization and timezone handling.
3. Implement Open-Meteo weather fetch and ArduinoJson parsing (endpoint verified live: 610 B payload for Burwood East).
4. Implement dual content fetch: Scripture Verse + Vocabulary Word (BibleGateway VOTD JSON verified live; Wordnik / Merriam-Webster still to evaluate).

## Layout follow-ups (from the 2026-09-12 review, not yet done)

- Portrait weather strip gives an alert only one 12 px line, so a real alert still
  loses its tail to the overflow marker. Consider 20 px starting at y=276.
- Portrait verse block leaves ~90 px of dead space below a short verse before the
  reference rule; consider vertical centring.
- `drawWrappedText*` geometry constants are repeated across the five layout functions;
  a single block-descriptor would remove the duplication.
- `main.cpp` still ships a hardcoded fixture and never sleeps; both are Phase 2/4 work.
