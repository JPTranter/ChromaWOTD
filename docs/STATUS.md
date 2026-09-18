# CHROMAWOTD — Status

**Updated:** 2026-09-18
**Phase:** 5 — Buttons, time-based content, device configuration (NVS) & setup portal (complete)
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
| Secret scanning | ✅ Audited: **no secrets in any of the 79 commits** (verified with gitleaks over full history); `secrets.h` (live Wi-Fi creds) has never been tracked. Gap was the guard, not the history — ported eClock's protections: `.gitleaks.toml`, `.pre-commit-config.yaml` (gitleaks + hygiene hooks, installed), `.gitattributes`, widened `.gitignore`, and a CI `secrets` job scanning all history. Both layers tested against a planted key — correctly blocked (see LESSONS §41) |
| Device configuration (NVS) | ✅ Resolution order **NVS → built-in defaults**, one shared instance (`config/`). Portal-written values persist in NVS (`0x9000`), so an **app-only** flash keeps them while the merged image at `0x0` yields a factory-fresh device. Fixed a latent divergence: host and device previously compiled *different* fallback coordinates (Melbourne vs Sydney) (see LESSONS §43) |
| First-boot setup portal | ✅ SoftAP `ChromaWOTD-<MAC>` + DNS catch-all + config form at `192.168.4.1` (with a Wi-Fi QR code), AP password regenerated per boot and shown on the ePaper. Factory reset by 10 s button hold. **Verified end-to-end on hardware**: scan/submit → NVS write → reboot → joined the home network → rendered real content |
| No compiled-in credentials | ✅ Credentials exist **only** in NVS, written by the setup portal. `secrets.h` and its template were removed deliberately — no build, local or CI, can produce an image containing them. `tools/merge_firmware.py` keeps a tripwire scan in case a credential path is ever reintroduced (see LESSONS §44) |
| Offline/failure honesty | ✅ A failed fetch no longer invents data: no fabricated `0°C` (the struct used to default to 0.0 and render a confident reading), and no canned verse/word presented as today's. `WeatherData::valid` separates "no reading" from a genuine 0°C; a failed Wi-Fi shows OFFLINE, a single failed API while online shows PARTIAL with whatever succeeded (see LESSONS §44) |
| Stored config can be wiped by the NVS stack | ⚠️ **DIAGNOSED, not fixed.** The 20 KB NVS partition is shared with WiFi/BLE/DHCP state; when it fills, the Arduino core's `nvs_flash_init()` erases and reformats the WHOLE partition, silently destroying the stored credentials (the device just re-offers the setup portal). Our entries were well formed and no code of ours erased them. Needs a dedicated NVS partition and/or a read-back check after save (see LESSONS §45) |
| Weather API timezone | ✅ Fixed: the URL sent a POSIX TZ string, which Open-Meteo rejects with HTTP 400, so **every** weather fetch had been failing silently. Now `timezone=auto`; the live tests fail (not skip) when the endpoint is reachable but the request is malformed |
| Public repo & CI | ✅ Published at [github.com/JPTranter/ChromaWOTD](https://github.com/JPTranter/ChromaWOTD) (public, `master`). The workflow had **never run** before the push; the first run exposed four pre-existing breakages, all fixed — firmware could not build without `secrets.h` (now `__has_include` + defaults), `.clang-format` used pre-v18 enum spellings, the lint gate used an unpinned formatter version (now `clang-format==23.1.1`), and `weather_icon_sheet.png` was a ledger orphan. **All 3 CI jobs green** (see LESSONS §42) |
| Phases 2–5 review fixes | ✅ `docs/CODE_REVIEW.md` findings addressed 2026-09-18: portal-timeout fall-through no longer wipes the setup screen (BUG-01), the portal's content mode is honoured (BUG-02), the setup form pre-fills real defaults instead of `(0,0)` and keeps the typed SSID (BUG-03/INC-04), the e2e portal tool's default timezone is an IANA name (BUG-04), NVS writes are actually checked and empty values delete their key (BUG-05/DES-03), TLS pins **roots** instead of Let's Encrypt intermediates (DES-01), an unset clock is reported as such rather than as an API outage (DES-02), plus the stale pin comment, duplicate macro, host coordinate source, SoftAP teardown, bench-tool port and the three documentation drifts. See LESSONS §46. **BUG-06 (NVS wipe) deliberately still open** — needs a bench session |

## Next steps

> Two critical reviews exist: [`docs/REVIEW.md`](REVIEW.md) (Phase 1 — all findings
> resolved) and [`docs/CODE_REVIEW.md`](CODE_REVIEW.md) (Phases 2–5). The Phases 2–5
> findings were addressed on 2026-09-18 except BUG-06; see LESSONS §46 and the status
> table above. The list below is the remaining product work.

1. **NVS wipe on a full partition (BUG-06 / LESSONS §45) — DIAGNOSED, not fixed.**
   The 20 KB `nvs` partition is shared with the Wi-Fi/BLE/DHCP stack, and the Arduino core
   reformats the whole partition when it runs out of room, silently destroying the stored
   credentials (the device then just re-offers the setup portal). The fix is a dedicated
   partition: `firmware/partitions.csv` + `board_build.partitions`, with
   `Preferences::begin("chromawotd", false, "nvs_cfg")`.
   **Deliberately not done here:** changing the partition table moves flash offsets, so it
   needs a FULL flash and hardware verification, plus an update to the app-only-flash
   workflow (`tools/merge_firmware.py` and the docs). It cannot be validated without a
   board on the bench.
2. **TLS trust roots (DES-01) — fixed 2026-09-18.** The pinned Let's Encrypt intermediates
   (YR2 / YE1) are replaced by a root bundle (ISRG Root X1 + ISRG Root X2 + Amazon Root
   CA 1), so a server may rotate its intermediate without breaking the fetches. Verified on
   the host against the live chains of all three hosts; **not yet exercised on hardware**
   in this session (no port attached).
3. **Cached last-good content** — show the previous verse/word with an "as of" note instead
   of the "unavailable" screen. Real data rather than invented data; still on PROJECT_PLAN.
4. **Weather icon mapping for Drizzle / Rain 61–67** — accepted known limitation
   (LESSONS §35); revisit only if the text-only distinction proves insufficient.

## Layout follow-ups (from the 2026-09-12 review, not yet done)

- [x] ~~Portrait strip/verse-block issues~~ Resolved by dropping portrait mode entirely
  (single landscape, light presentation, 2026-09-12).
- `drawWrappedTextCentered`/`drawVerseBlock` geometry constants could still move into a
  block-descriptor type, but it is far lower priority with only one layout.
- ~~`main.cpp` still ships a hardcoded fixture and never sleeps; both are Phase 2/4 work.~~
  **Stale (removed 2026-09-18):** `main.cpp` runs the real sync → render → deep-sleep cycle;
  the fixture only survives in the host preview tooling (`layout_render`).
