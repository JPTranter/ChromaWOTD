# ChromaWOTD — Code & Project Review

**Reviewed:** 2026-09-12 · **Firmware:** 0.1.0 (Phase 1 complete)
**Scope:** `firmware/`, `tools/`, and the `docs/` guidance that surrounds them.
**Basis:** the committed tree (commits `6760fd6..408f423`); findings are grounded in the
on-disk source at those hashes, not in memory.

This document is a *critical* review: it lists what to fix and why, ordered by impact.
It is deliberately blunt. Treat items as a backlog, not a punch list to clear in one go.

Severity: **P0** security/correctness that must be fixed before shipping network code ·
**P1** architecture that will compound if left · **P2** readability/docs/consistency ·
**P3** hygiene/nicety.

---

## 1. Security

### S1 (P0) — TLS certificate validation is not addressed for the network stage
The entire Phase 2–3 roadmap (`docs/PROJECT_PLAN.md`) is HTTPS network I/O — Open-Meteo,
BibleGateway, BoM, NTP. The Arduino `HTTPClient`/`WiFiClientSecure` stack **does not
validate the server certificate against a trusted root CA by default**; on ESP32 it needs
`client.setCACert(...)` (or `setInsecure()` — which you must *not* call). None of this is
planned or documented anywhere.

*Recommendation:* add a `Security` note to the plan and README now, and — when Phase 3
lands — ship the relevant root CA(s) (or a full CA bundle) and call `setCACert` before
every request. Document explicitly that `setInsecure()` is forbidden. For a device that
pulls scripture + weather and later stores Wi-Fi credentials via a captive portal, an
unauthenticated TLS connection is a real MITM surface (DNS/AP spoofing → injected verse,
weather, or, worse, credential harvest through a fake portal).

### S2 (P1) — `cc_utf8ToAscii()` reads past the declared length, trusting NUL-termination
`verse_display.cpp:21-49` reads `p[1]`, `p[2]`, `p[3]` with only NUL checks, while its
caller `cc_countGlyphsN()` (`:53-60`) advertises a *bounded* length (`end = p + len`). The
decoder is called with a length-bounded substring but never told the bound, so a
multi-byte sequence that straddles the `len` boundary is still fully decoded. Today every
input is a NUL-terminated C string, so no byte is read past the allocation — but that is an
*accident of the current callers*, not a property of the API.

*Recommendation:* pass a `size_t len` into the decoder (or make `cc_countGlyphsN` clamp a
copy), and add a unit test feeding truncated/overlong sequences (e.g. a lone `0xF0`, a
`0xE2 0x80` cut mid-grapheme) to prove it never reads OOB. This is exactly the class of bug
a fuzz/length-assertion test should own.

### S3 (P1) — No credential-handling policy documented
`secrets.h` is gitignored, but there is **no `secrets.h.example`** and no statement of what
belongs there versus what goes to NVS via the Phase-2 captive portal. There is also no
"never log credentials" rule, which matters because the device already uses `Serial.printf`
freely.

*Recommendation:* add `firmware/secrets.h.example` (empty template + comments), and a short
"Security model" section in the plan: Wi-Fi SSID/passphrase and timezone/location live in
NVS only, are entered over the portal, are never echoed to serial, and the portal AP
password is per-boot random.

### S4 (P2) — Remote strings reach the renderer as untrusted input
BibleGateway returns HTML (`content` carries `<h3>`/`<span class="small-caps">`) and
entity-encoded `text`. The mitigation is "only parse `text`/`reference`" (already in
`docs/research/SCRIPTURE_APIS.md`) plus fixed buffers in the renderer. That is acceptable
for a single-user device, but the invariant is implicit.

*Recommendation:* state it once: *remote text is untrusted input; the renderer's fixed
buffers (`wordBuf[64]`, `lineBuf[96]`) are the boundary; never `sprintf` remote text; only
ever `snprintf`/bounded-copy.* Add a comment at those two buffers.

### S5 (P3) — No signed-update / OTA story
Not needed for a personal ambient display, but if this ever becomes a shared/shipped
device, OTA images are unsigned and the portal is a credential surface. Note it in the
plan's release-tagging item so it's a conscious decision, not an oversight.

---

## 2. Design & architecture

### D1 (P1) — `verse_display.cpp` is a 735-line monolith with five unrelated concerns
One file currently holds: (1) UTF-8 decoding, (2) text wrapping/measurement/overflow,
(3) weather-icon vector drawing, (4) the host-vs-device backend (color mapping + `dev_*`
primitives), and (5) five near-identical layout functions. Any change to "how text wraps"
or "how colours map" risks touching all of it.

*Recommendation:* split along those seams (see §3 module map). The layout functions then
become a small, reviewable "view" layer on top of stable primitives.

### D2 (P1) — The five layout functions duplicate geometry ~5× with magic numbers
`drawLayoutPortrait`, `drawLayoutPortraitInverted`, `drawLayoutLandscape`,
`drawLayoutLandscapeInverted`, `drawLayoutLandscapeDark` share the same four zones
(header / verse / reference / weather) but each re-states every coordinate. Values like
`22`, `204`, `244`, `296`, `205`, `251`, `215`, `72`, `84`, `58`, `78` are unexplained and
repeated. A design change (e.g. "give the alert 20px") must be made in five places, and
has already drifted (see D3).

*Recommendation:* introduce shared zone helpers — `drawHeader(...)`,
`drawVerseRegion(...)`, `drawReference(...)`, `drawWeatherStrip(...)` — parameterised by a
small `LayoutMetrics`/theme struct, plus a named-constants block that documents the pixel
budget (e.g. why the header is 22px, why the reference sits at 204, why the weather strip
starts at 244).

### D3 (P1) — The theme model is inconsistent across orientations
Portrait has *light* and *inverted*; landscape has *light*, *inverted* and *dark*. The
`drawLayout()` dispatcher (`verse_display.cpp:719`) can only reach light/inverted and
silently cannot render "dark portrait" — there is no dark portrait layout at all, and no
place that says so except a comment inside the render tool.

*Recommendation:* define `enum class Theme { Light, Inverted, Dark }` and a single
dispatcher over `(Orientation, Theme)`. Either implement the missing combinations or make
the unsupported ones an explicit compile-time/asserted rejection, and reflect that in the
README table so "which theme works where" is not inferred by the reader.

### D4 (P2) — `WeatherData::icon` is a magic `int` 0–3
The mapping (`0=sun, 1=cloud, 2=rain, 3=partly`) exists only as a header comment
(`verse_display.h:18`). The same values are string-matched in three different places
(`tools/preview/verse_template.html`, `tools/render_preview.py`, `layout_render.cpp`).

*Recommendation:* `enum class WeatherIcon : int { Sun = 0, Cloud, Rain, PartlyCloudy };`
shared by the header; derive the string↔enum tables from one source.

### D5 (P2) — Two sources of truth for UTF-8 handling (dead code in the mock canvas)
`verse_display.cpp` now pre-normalises text before any draw call, so the mock canvas's own
decoding — `drawChar`'s `ch >= 128 → '?'` fallback (`canvas.cpp:162`) and `drawString`/
`measureText`'s degree/dash byte-scanning (`canvas.cpp:190-214`) — is **bypassed** and is
now latent dead code that still *looks* authoritative.

*Recommendation:* strip the UTF-8 handling out of `canvas.cpp` and add a one-line contract
comment: *the canvas receives only ASCII bytes plus the `0xB0` degree sentinel.* Keeping
one decoder (the shared one) removes a whole class of "host and device disagree" bugs.

### D6 (P2) — No backend abstraction: `dev_*` shims duplicated under `#ifdef`
The two `#ifdef CHROMAWOTD_HOST` blocks (`:86-159`) are ~70 lines of near-identical
forwarding that differ only in colour mapping and degree-symbol handling. Adding a third
target (a real framebuffer, a desktop window) means a third copy.

*Recommendation:* define a small `DisplayTarget` interface (fillRect/drawRect/lines/circle/
fillCircle/drawChar/measureText) with two implementations (`SeeedTarget`, `CanvasTarget`),
and have the layout code call through a `target` reference. This also makes the
degree-symbol and colour mapping single-purpose per target instead of entangled in
`dev_drawString`.

### D7 (P2) — No application state machine
`main.cpp` is a demo: `setup()` draws one hardcoded fixture, `loop()` spins `delay(1000)`.
There is no model of *wake → sync → render → sleep → error* that the plan's Phases 2–4
require, and the plan doesn't sketch one either.

*Recommendation:* document the intended top-level state machine (states: `Boot`,
`FirstBoot/Setup`, `Sync`, `Render`, `Sleep`, `Error/Offline`) in `docs/ARCHITECTURE.md`
*before* implementing Phase 2, so buttons, NVS, deep sleep and the offline banner have a
home rather than being bolted onto `loop()`.

---

## 3. Proposed module map (addresses D1, D2, D5, D6)

```
firmware/src/
  verse_display.h           data structs + enums (VerseData, WeatherData, WeatherIcon,
                            Theme, Orientation) — the public contract
  verse_display.cpp         the 5 layout view functions only (thin, geometry + zone calls)
  text/
    glyphs.{h,cpp}          cc_utf8ToAscii, cc_countGlyphs* — one decoder, length-bounded
    wrap.{h,cpp}            wrapping, cc_wrappedLineCount, cc_lineCapacity, cc_lineBudget,
                            drawOverflowMarker, the drawWrappedText* helpers
  draw/
    weather_icon.{h,cpp}    drawWeatherIcon
    target.h                DisplayTarget interface
    target_seeed.cpp        Seeed GFX backend (colour mapping, degree circle)
    target_canvas.cpp       host canvas backend (mock + PNG)
  main.cpp                  application state machine (Phase 2+)
```

The mock canvas then lives only under `firmware/test/` and stops carrying product logic.

---

## 4. Correctness & robustness (secondary)

### C1 — Unbounded word/line copies (see S2/S4)
`wordBuf[64]` (`:314`, `:449`) and `lineBuf[96]` (`:371`) silently truncate. In
`drawWrappedTextCentered` a line longer than 95 bytes is cut **mid-word with no marker**.
Today lines are ≤ ~14 glyphs so it is latent, but the cap is undocumented.

### C2 — `cc_lineBudget` reservation silently turns off in narrow columns
`cc_lineBudget` (`:262-268`) returns `maxW` (no reservation) when `maxW - slack*w < 4*w`.
In that case `drawOverflowMarker` degrades to `.`. Acceptable, but the interaction between
"no reservation" and "single-dot marker" is not stated; add a comment or an assertion.

### C3 — Weather icons are untested and heavy on integer division
`drawWeatherIcon` uses `size/4`, `size/5`, `size/6`, `size/10` with no floor guidance; the
two call sites (26px portrait, 24px landscape) happen to look fine. No test exercises any
icon case, so a refactor or a new size would regress invisibly.

### C4 — `cc_roundTemp` is correct but the `+0.5f` idiom still exists in prose
`layout_render.cpp` and older comments describe `(int)(t+0.5f)`; only `cc_roundTemp` is
correct for negatives. Sweep any remaining prose/example that suggests the truncation idiom.

---

## 5. Readability & comments

- **R1** Name the magic numbers (see D2). A `kHeaderH=22`, `kVerseRegionTop=28`,
  `kReferenceY=204`, `kWeatherStripTop=244`, `kSplitX=205`, `kForecastCenterX=251`,
  `kIconCx/Cy`, etc., each with a one-line rationale, would make the layout self-documenting.
- **R2** Add file-level doc comments to `verse_display.h` and `.cpp` stating the layering:
  *"view layer → shared text/glyph primitives → backend target; never draw before deciding
  truncation; never send multi-byte UTF-8 to a target."*
- **R3** Remove the dead `C_WHITE/C_BLACK/C_RED/C_YELLOW` legacy aliases
  (`verse_display.h:28-32`) — zero call sites.
- **R4** Comment the vertical budget: `availH = w.alert ? 10 : 20` and `if (curY < 284)
  curY = 284` (`:500, 505`) encode "the icon+temp occupy 244..284, leaving N px for
  condition/alert" — that rationale is invisible.
- **R5** Settle one comment convention (full sentences, present tense) and apply it; the
  file mixes terse imperative fragments with prose.

---

## 6. Documentation gaps

- **DOC1 (P1)** No `LICENSE`. This is a personal project but a future developer cannot
  legally reuse anything without one. Add MIT (or an explicit "All rights reserved" note).
- **DOC2 (P1)** No `CONTRIBUTING.md`, `.clang-format`, or `.editorconfig`. The build/test/
  verify workflow and the "docs/images must byte-match ctest" rule currently live only in
  LESSONS and the skill. A short `CONTRIBUTING.md` plus a `.clang-format` (and a
  `git blame`-friendly formatting commit) is the single highest-leverage onboarding item.
- **DOC3 (P2)** No CI. `tools/verify_all.py` already exits non-zero and is CI-safe. A
  minimal GitHub Actions workflow running `python tools/verify_all.py --skip-firmware`
  (host tests + render ledger) on push/PR would enforce the ledger invariant automatically.
- **DOC4 (P1)** No architecture doc. The data flow (fetch → `VerseData`/`WeatherData` →
  layout → single `update()`), the refresh/power sequence (draw → `update()` → wait BUSY →
  `ENABLE` low → deep sleep), and the security model are scattered across LESSONS 13/16 and
  the plan. Consolidate into `docs/ARCHITECTURE.md` and link it from the README.
- **DOC5 (P2)** No `secrets.h.example` (see S3).
- **DOC6 (P2)** README has no "Security" note (see S1).
- **DOC7 (P3)** `chroma_version.h` is a hardcoded `"0.1.0"` with no release procedure tying
  it to git tags; add a one-line release checklist (bump version, run `verify_all.py --clean`,
  tag) to the plan's release-tagging item.

---

## 7. Repo hygiene

- **H1 (P3)** A stray `output/` directory sits at the repo root (a leftover from running a
  test binary with the wrong working directory). It is gitignored so it does not pollute
  history, but it is cruft — delete it.
- **H2 (P2)** `firmware/include/board_pins.h` is a 3-line "deprecated" stub with **zero
  references**. Delete it, or it will invite future code to `#include` a dead file.
- **H3 (P2)** `driver.h` re-declares `BOARD_SCREEN_COMBO=512` / `USE_XIAO_...EE05` "for the
  Arduino-IDE build path" while `platformio.ini` also passes them globally — a second
  source of truth. Either drop the `driver.h` fallbacks (PlatformIO is the only build
  path) or move *all* board config to one file that both paths consume. Also: `driver.h`'s
  header comment still says "JD79661" with no JD79667 mention, contradicting the README's
  clarification.
- **H4 (P3)** `.vscode/` is gitignored but the repo has no shared editor/formatting config,
  so contributors diverge on style (see DOC2).

---

## 8. Testing gaps

- **T1 (P1)** No tests for: `toDeviceColor` colour mapping, `cc_lineCapacity` edge cases
  (0/negative `maxH`, tiny `lineHeight`), any weather-icon case, or adversarial UTF-8
  input (see S2). The icon and decoder are the two places a refactor would regress
  silently.
- **T2 (P2)** The device `#else` backend is only exercised by a *manual* firmware build;
  the host suite compiles the `#ifdef CHROMAWOTD_HOST` branch only. A compile-only CI job
  (`pio run -e s3`) — even without flashing — would catch a broken device branch at PR time.
- **T3 (P3)** The render ledger is byte-for-byte (good) but there are no *stored golden*
  hashes gated in CI; the invariant is only enforced when someone runs `verify_all.py`
  locally. Fold `--skip-firmware` into CI (DOC3) to close this.

---

## 9. What is already good (do not regress)

- The **dual-target split** and the discipline that host renders must byte-match the
  device path is the right call given 25 s physical refreshes, and the PNG archive +
  `verify_all.py` ledger make it enforceable.
- The **shared UTF-8 decoder + glyph-counted measurement** is the correct fix for the
  "host and device disagree" class; the remaining work (D5) is *removing* the redundant
  second decoder, not re-adding one.
- The **visible-truncation invariant** (reserve marker space *before* drawing) and the
  **region-containment tests** are exactly the kind of property-based check firmware needs.
- The **LESSONS_LEARNT** file is genuinely load-bearing: DTR/RTS download-mode latch,
  CP437 degree trap, the inert `lib_build_src_filter` — each is the kind of finding that
  otherwise costs hours.

---

## 10. Implementation order & priority

Not every finding matters at the same moment. The ordering below is driven by one
question — *what un-blocks the most later work, and what must land before the next
product phase?* — rather than by raw severity. Phases are self-contained enough to
commit independently, and each lists the findings it clears.

**Hard gate (before any Phase 3 network code ships):**
- **S1, S3, DOC4** — the TLS-validation policy, the credential-handling policy, and an
  `docs/ARCHITECTURE.md` capturing the data flow, refresh/power sequence and security
  model. These are the three things that turn the planned HTTPS fetch from a prototype
  into a defensible design, and they are cheap now and expensive to retrofit after
  `WiFiClientSecure` calls are already scattered through `main.cpp`.

**Phase A — structural refactor (clears D1, D2, D3, D5, D6; un-blocks most of the rest).**
1. Define `Theme`/`Orientation`/`WeatherIcon` enums and make `drawLayout()` the single
   dispatcher (D3, D4) — small, mechanical, removes the hidden "no dark portrait" trap.
2. Split `verse_display.cpp` along the §3 module map: `text/glyphs`, `text/wrap`,
   `draw/weather_icon`, `draw/target` + `target_seeed`/`target_canvas`, leaving a thin
   view layer (D1, D6).
3. While doing (2), delete the mock canvas's redundant UTF-8 decoder and the legacy
   `C_*` aliases (D5, R3), and introduce a named-constants block for the layout geometry
   (D2, R1, R4).
   *Order within A matters: do the enums first, then the module split, then the cleanup,
   so each step compiles and `verify_all.py` stays green throughout.*

**Phase B — dead code & hygiene (clears H1, H2, H3, R5, C4).**
Delete `board_pins.h` and the stray root `output/` dir; settle `driver.h` vs
`platformio.ini` as a single source of truth; sweep prose that still suggests the
`(int)(t+0.5f)` truncation idiom.

**Phase C — the "professional firmware" layer (clears DOC1, DOC2, DOC3, DOC5, DOC6, T1, T2).**
LICENSE + `CONTRIBUTING.md` + `.clang-format`/`.editorconfig` + `secrets.h.example`, a
GitHub Actions workflow running `verify_all.py --skip-firmware` (and a compile-only
`pio run -e s3` job), and the missing tests (colour mapping, `cc_lineCapacity` edges,
weather-icon cases, adversarial UTF-8). This phase is what makes the repo legible to a
developer who has never met it — do it before opening the code to others.

**Phase D — robustness hardening (clears C1, C2, C3, S2).**
Length-bounded glyph decoding + the truncated-input test; a decision (and comment) on the
narrow-column marker degradation; `snprintf`-only rule on remote text. These are all
small once Phase A's module boundaries exist.

**Phase E — deferred / conscious decisions (clears S5, DOC7, D7).**
Signed-update posture, the `chroma_version.h` release checklist, and the top-level
application state machine (state machine is best designed *before* Phase-2 buttons/NVS,
so pull just that piece forward if Phase 2 starts).

**Relative effort (rough):** A ≈ M (largest single chunk, but mostly mechanical) · C ≈ M ·
D ≈ S–M · B ≈ S · hard-gate ≈ S. Suggested sequencing: hard-gate → A → B → C → D, with E
scheduled against the corresponding product phase.

Rationale for not simply going top-to-bottom by severity: S2 and C1–C3 are real but are
latent *today* (NUL-terminated inputs, narrow-but-not-tiny columns); they become cheap and
testable only after the Phase-A boundaries put the decoder and wrappers in their own
modules. Paying down the monolith first is what makes the correctness fixes small instead
of another edit inside a 735-line file.
