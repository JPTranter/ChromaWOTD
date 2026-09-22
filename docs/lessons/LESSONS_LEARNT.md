# ChromaWOTD — Lessons Learnt

Inherited from the sibling eClock project (read `../eClock/docs/lessons/LESSONS_LEARNT.md`
for the full history). New lessons get appended here with continuing numbers,
starting after eClock's highest section.

## 1. (inherited) Paged loop rule
Draw every pixel of a frame inside `firstPage()/do/while(nextPage())`. Content
drawn before `firstPage()` is wiped from the buffer.
> **Not applicable on ESP32-S3 / Seeed GFX.** Lessons 1–3 are nRF52840 + mbed
> mechanics from eClock; this project renders into the Seeed GFX buffer and calls
> `epaper.update()` once. Kept for provenance only.

## 2. (inherited) Never reconfigure GPIO on SPI pins
The panel shares SPI pins; touching `PIN_CNF`/pin config on those pads kills the
SPI bus (`Busy Timeout!`).

## 3. (inherited) No manual clears before first refresh
`display.init(115200, true, ...)` performs the initial full white refresh
itself; extra manual clears trigger bus timeouts.

## 4. (inherited) Windows Python encoding
Always `open(..., encoding='utf-8')` — cp1252 default corrupts UTF-8.

## 5. Seeed_GFX + PlatformIO integration
Seeed_GFX is a **flat-layout Arduino library**: only root `TFT_eSPI.cpp` is a TU
and it `#include`s Processors/, Extensions/ (incl. EPaper.cpp) and
Touch_Drivers/ itself. Under PlatformIO it must be handled specially:
1. The panel+board selection (e.g. `BOARD_SCREEN_COMBO 512` =
   Setup512_..._2inch9_BWRY, `USE_XIAO_EPAPER_DISPLAY_BOARD_EE05`) must be
   passed as **global build_flags**, not per-sketch defines, or TUs disagree.
2. `#include "TFT_eSPI.cpp"` from main.cpp and never compile the subfolder sources
   standalone — they assume `TFT_eSPI.h` is already included. **Do not add
   `lib_build_src_filter` to platformio.ini**: it is not a PlatformIO option
   (PIO 6.1.19 prints `Warning! Ignore unknown configuration option
   lib_build_src_filter` on every build and ignores it). The subfolders stay out of
   the build on their own because PlatformIO compiles only the root directory of a
   legacy-layout library — verified by a clean build emitting exactly one Seeed_GFX
   object, `lib*/Seeed_GFX/TFT_eSPI.cpp.o`. (Verified 2026-09-12.)
3. Stock PlatformIO has no `seeed_xiao_esp32s3_plus` board; use
   `seeed_xiao_esp32s3` (Seeed GFX drives panel pins by raw GPIO number).
4. Seeed_GFX is not on the PlatformIO registry — depend on the GitHub URL.
   Verified combo for the 2.9" quad-colour panel: 512.
   EE05 pin map (from EPaper_Board_Pins_Setups.h): SCLK 7(D8), MOSI 9(D10),
   CS 44(D7), DC 10(D16), BUSY 4(D3), RST 38(D11), ENABLE 43(D6), no MISO.

## 6. Partial refresh is not available on the 2.9" BWRY panel
`USE_PARTIAL_EPAPER` is only defined for monochrome panels (SSD1680/81/83,
UC8179, ED103TC2) — there is no partial path in the JD79667 driver defines, so every
update is a full ~25 s sweep with multi-sweep flickering. Decided 2026-09-12:
clock-style content refreshes a few times a day at most; red/yellow reserved
for alerts and highlights, black carries text.

**Controller naming (2026-09-12 review).** Two names circulate for this panel and both
are defensible: Seeed's product datasheet for SKU 104990855 lists "Driver IC: JD79661",
while Seeed GFX's combo table maps `BOARD_SCREEN_COMBO=512` to "2.9 inch BWRY ePaper
Screen (JD79667)" and the build instantiates `TFT_Drivers/JD79667_Defines.h` +
`JD79667_Init.h`. Reference the library path when talking about the build, and the
datasheet when talking about the hardware — don't keep re-litigating which is "right".

## 7. Seeed_GFX header inclusion subtleties
`TFT_eSPI.h` automatically `#include`s `Extensions/EPaper.h` near line 1133.
Because `Extensions/EPaper.h` does not have its own `#ifndef` inclusion guard,
any code that includes both `TFT_eSPI.h` and `Extensions/EPaper.h` will trigger
a `redefinition of 'class EPaper'` compiler error. Separate translation units
should only `#include "TFT_eSPI.h"`.

## 8. Dual-target layout engine & host harness
To iterate rapidly without 25-second display flashes and serial uploads:
- Keep the layout code pure C++ in `verse_display.cpp`, abstracting draw calls
  (`fillRect`, `drawString`, `drawLine`, `drawCircle`, `drawWeatherIcon`).
- Under `#ifdef CHROMAWOTD_HOST`, route to `CcCanvas` which renders into an
  RGBA buffer using a vendored 5x7 bitmap font (`glcdfont.c`) and outputs PNGs
  via `stb_image_write.h`.
- Under target build, route directly to `epaper` (`TFT_eSPI`), mapping semantic
  colors (`CC_WHITE`, `CC_BLACK`, `CC_RED`, `CC_YELLOW`) to Seeed GFX constants.
- Unit tests in `firmware/test/` verify geometry and output PNG screenshots.

## 9. ESP32-S3 USB Serial/JTAG download mode after flashing
When flashing over the native USB Serial/JTAG port on Windows, the default RTS/DTR
reset issued by `esptool.py` can leave strapping pin GPIO0 asserted LOW, keeping
the ROM bootloader in download mode (`boot:0x21 (DOWNLOAD(USB/UART0))`) instead
of booting from flash. Toggling DTR high/de-asserted and pulsing RTS restores
SPI boot mode (`boot:0x29 (SPI_FAST_FLASH_BOOT)`).

## 10. UTF-8 on TFT_eSPI / Seeed_GFX — one shared decoder
`epaper.drawChar()` expects CP437/ASCII single bytes; standard UTF-8 literals like
`"%d°C"` embed multi-byte sequences and draw one garbage glyph per byte. Rather than
sprinkle special cases, every draw path now routes text through `cc_utf8ToAscii()` in
`text/glyphs.cpp` (was `verse_display.cpp` pre-D1), which consumes one *glyph* at a time and emits one ASCII byte:

| Input glyph | Emitted |
| :--- | :--- |
| `°` U+00B0 (`C2 B0`) | `0xB0` sentinel → drawn as a vector circle (`drawCircle`), never a glyph |
| `‘ ’` U+2018/19, `′` U+2032 | `'` |
| `“ ”` U+201C/1D | `"` |
| `– — ―` U+2013/14/15 | `-` |
| `…` U+2026 | `.` |
| `⚠` U+26A0 | `!` |
| NBSP `C2 A0` | space |
| anything else ≥ 0x80 | one `?` per glyph (not per byte) |

`cc_countGlyphs()`/`cc_countGlyphsN()` mirror the decoder so measured width always
equals drawn width, and word wrapping counts glyphs rather than bytes. Consequence to
remember: **host and device now agree**, because the host canvas's own UTF-8 handling is
no longer relied upon — normalisation happens before either backend sees the text.
(2026-09-12)

## 11. Archiving Layout Screenshots for Lessons Learned
To preserve visual design iterations and prevent regressions, host-rendered layout
PNGs generated during testing are archived in
[`docs/images/history/`](../images/history/) — a RELATIVE link, deliberately: this used to
be an absolute `file:///C:/Users/...` URL, which resolves only on the machine that wrote it.
This tracks light, inverted, dark, alert and overflow-marker variants across portrait
and landscape orientations for future reference.

Note the two folders have different jobs, and confusing them misleads: `docs/images/history/`
is the **design record** (renders the decisions were made from, archived by hand), while the
PNGs one level up in `docs/images/` are the **regression ledger** (byte-compared on every
`verify_all.py` run, and rendered from the default host build where the proportional font is
compiled out — so they show the 5×7 fallback, not the shipped look). The chronology of the
presentation itself is [`docs/UI_HISTORY.md`](../UI_HISTORY.md).



## 12. ESP32-S3 Internal RTC, SNTP Synchronization, and Daylight Saving Time (DST)
- **Does the board have an RTC?**
  - **Internal RTC**: Yes. Unlike the nRF52840 on eClock (which had no persistent RTC counter across power cycles/deep sleep), the ESP32-S3 contains an internal RTC controller powered by the low-power RTC domain. It keeps track of time (microseconds) through deep sleep cycles.
  - **External Battery-Backed RTC chip**: No discrete battery-backed RTC chip (e.g. DS3231 or PCF8563) exists on the XIAO ESP32-S3 or the EE05 board. Time is preserved across deep sleep, but if power/battery is completely disconnected, the RTC resets to epoch 0 (`1970-01-01`).
- **SNTP Synchronization**:
  - Time is synchronized against NTP (`pool.ntp.org`) on boot and during every scheduled Wi-Fi wakeup using the ESP-IDF SNTP client (`configTzTime()`).
  - Resyncing on scheduled wakeups corrects for the internal RC oscillator drift (typically ~1–2% drift on the internal 150 kHz RC without a 32.768 kHz external crystal).
- **Daylight Saving Time (DST) Handling**:
  - Rather than hardcoding fixed hourly offsets, timezone configuration uses standard **POSIX timezone strings** (e.g. `AEST-10AEDT,M10.1.0,M4.1.0/3` for Sydney/Melbourne, or `EST5EDT,M3.2.0,M11.1.0` for US Eastern).
  - The underlying standard C library (`newlib` / `time.h`) evaluates the DST transition rules automatically on every `localtime()` call. No manual clock adjustment or seasonal firmware update is needed.

## 13. E-Paper Bistability & MCU Deep Sleep Power Behavior
- **Does deep sleep affect display quality?**
  - **No.** Electrophoretic (ePaper) displays are **inherently bistable**. Micro-capsules containing physical titanium dioxide (white) and carbon/pigment (black, red, yellow) particles are moved into place by an electric field during the refresh cycle. Once positioned, they are held by molecular (van der Waals) forces with **zero power required**.
- **Power sequence before entering ESP32-S3 deep sleep**:
  1. Complete full display refresh (`epaper.update()`), which automatically sends `EPD_SLEEP()` (power saving command to JD79661 display controller).
  2. Wait for BUSY pin (GPIO 4) to go LOW/idle.
  3. Turn OFF the panel supply rail MOSFET via `digitalWrite(TFT_ENABLE, LOW)` (D6 / GPIO 43). This eliminates quiescent leakage through the panel controller and boost circuitry while sleeping.
  4. Call `esp_deep_sleep_start()`.
- **Display Retention & Quality**:
  - The rendered image remains static and crisp on the screen with 100% optical density for weeks or months with zero power applied.
  - Image quality is unaffected by the MCU being in deep sleep or completely unpowered.

## 14. 18650 Li-ion Battery Compatibility, USB Charging & Runtime Estimation
- **Battery Compatibility (NEXcell NEX-18650-2600 3.7V 2600mAh)**:
  - **Chemistry**: Standard 3.7V nominal Li-ion (4.2V float cutoff, 3.0V discharge cutoff). Fully compatible with the Seeed XIAO ESP32-S3 and EE05 power rails.
  - **Polarity Caution**: When connecting via the JST connector on the board or soldering directly to the battery pads under the XIAO, verify polarity: negative terminal must face closest to the USB-C port, positive terminal away from it.
- **Onboard Charging**:
  - **Yes**, the XIAO ESP32-S3 contains an onboard linear Li-ion charge management IC. When connected to USB-C 5V, it automatically charges the battery at ~50–100 mA (or up to ~350 mA depending on revision) with an onboard red `CHARGE_LED`.
  - For a 2600 mAh cell, a full charge from empty takes approximately 10–20 hours via the low-current onboard charger.
- **Runtime Calculations (4 refreshes/day @ 2600 mAh)**:
  - **Deep Sleep**: ~15 µA (0.015 mA) × 24 h ≈ 0.36 mAh/day.
  - **Active Cycles (4 per day)**:
    - Wi-Fi connect + NTP + Open-Meteo + Scripture fetch: ~100 mA for 4 seconds ≈ 0.11 mAh per sync.
    - Full screen refresh (JD79661 pigment sweep): ~60 mA for 25 seconds ≈ 0.42 mAh per refresh.
    - Total per cycle ≈ 0.53 mAh × 4 refreshes = 2.12 mAh/day.
  - **Total daily consumption**: ~2.5 mAh/day.
  - **Estimated Battery Life (2600 mAh 18650)**: $2600\text{ mAh} \times 0.85 \text{ (derating)} / 2.5\text{ mAh/day} \approx \mathbf{880\text{ days (over 2 years)}}$.
  - **Estimated Battery Life (500 mAh eClock pouch LiPo)**:
    - Usable derated capacity: $500\text{ mAh} \times 0.85 \approx 425\text{ mAh}$.
    - Runtime at 4 refreshes/day: $425\text{ mAh} / 2.5\text{ mAh/day} \approx \mathbf{170\text{ days (5.5 months)}}$.
    - Conservative real-world (including board leakage & button presses @ 5 mAh/day): $\mathbf{85\text{ days (almost 3 months)}}$.
    - Full recharge time on USB-C: ~2.5 to 5 hours (ideal match for the XIAO's ~100mA charge rate).
    - Form factor advantage: Flat pouch fits neatly inside the 3D-printed enclosure behind the 2.9" display, unlike the bulky 18mm cylindrical 18650.

## 15. Hardware LED for Sync Indication & Failure Reporting
- **No Partial Refresh on Quad-Colour Panel**: Because drawing even a 5-pixel circle triggers a ~25-second full pigment cycle, ePaper is unsuitable for transient "syncing..." spinners or badges during network fetches.
- **Onboard User LED (`LED_BUILTIN` / GPIO 21)**:
  - During Wi-Fi connect, NTP sync, and HTTP API fetches (duration ~2–4s), the onboard LED is pulsed gently or lit continuously to signal active background sync.
  - The LED turns off as soon as data reception finishes, right before the single physical ePaper sweep begins.
- **Error / Failure Indication**:
  - If Wi-Fi fails or an API returns an HTTP error, the system does not flash a transient error dialog.
  - Instead, the device redraws the screen with cached content and overlays a prominent **Red `[⚠ OFFLINE: <Reason>]` status banner** in the header.
  - An error blink cadence on the LED (e.g. 3 quick red blinks) provides immediate secondary diagnostic feedback.

## 16. Low Battery Monitoring & Graceful Screen Preservation
- **Battery Measurement Circuit**:
  - While the XIAO ESP32-S3 module lacks a dedicated internal battery voltage divider, the EE05 board (or an external 100kΩ/100kΩ divider connected to an RTC analog pin such as A0/GPIO1 or A1/GPIO2) enables battery voltage sampling.
- **Low Battery Thresholds (3.7V LiPo / Li-ion)**:
  - **Nominal Operating Range**: 3.5V to 4.2V.
  - **Warning Threshold (15% capacity, ~3.55V)**: Render a yellow battery outline icon in the top header row.
  - **Critical Cutoff Threshold (~3.30V)**:
    1. Draw a dedicated "LOW BATTERY — PLEASE RECHARGE" notice on the ePaper display with a red battery icon.
    2. Execute one final full refresh.
    3. Physically cut display power (`digitalWrite(TFT_ENABLE, LOW)`).
    4. Enter **infinite deep sleep** (disable periodic timer wakeups, enabling only wake-on-USB power or button press).
    5. The bistable ePaper permanently displays the recharge prompt without consuming any remaining battery energy.

## 17. Proprietary Vendor PDF Exclusion Policy
- Vendor datasheets, schematics, and pinout diagrams (from Seeed Studio and Good Display) are proprietary copyrighted assets and must **never be committed to the git repository**.
- `*.pdf` and `docs/hardware/datasheets/*.pdf` are explicitly gitignored.
- Local copies can be kept in `docs/hardware/datasheets/`, and `docs/hardware/datasheets/README.md` documents exact upstream download links and component mapping.

## 18. Development Tooling & Automation
To streamline workflow and prevent stale documentation artifacts:
- **`tools/regenerate_screenshots.py`**: A unified script that builds the CMake host test suite, executes all layout test assertions, captures fresh PNG renders, and synchronizes them directly into `docs/images/`. Run this whenever layout code in `verse_display.cpp` is touched.
- **`tools/esp32s3_reset.py`**: Automatically cycles DTR/RTS serial control lines to release the native USB-Serial/JTAG download bootloader latch after flashing without requiring manual cable unplugging or button pressing. Takes `--port` (required), `--baud`, `--watch`; it must not hardcode a COM port.
- **CMake/GoogleTest**: the host suite fetches googletest v1.14.0 from GitHub; `-DCHROMAWOTD_GTEST_DIR=<path>` reuses a local checkout for offline builds. Nothing may depend on another project's build tree.
- **Regeneration is the source of truth**: `docs/images/*.png` must always be byte-identical to a fresh `ctest` run (`md5sum` compare is a 5-second sanity check when reviewing a layout change).

## 19. Overflow must be visible, and the marker needs reserved space
`drawWrappedText*` and `drawVerseBlock` used to `break` when a block filled up,
silently swallowing the rest of a verse or alert. Fix, in three steps:
1. `cc_wrappedLineCount()` (greedy, mirrors the draw loop) vs `cc_lineCapacity()`
   decides *before drawing* whether content will be cut.
2. When it will be cut, `cc_lineBudget()` shrinks the final line's width budget by
   4 glyph widths (3 for the narrow weather slots, 6 for centred text) so the marker
   fits inline. Drawing the marker only *after* a full line does not work — there is
   no room left and it degrades to a misleading single `.` (this was tried; the
   landscape verse ended with a bare period).
3. `drawOverflowMarker()` appends `...`, or `..` when only that fits.
`test_layout_overflow` asserts the marker exists, that it degrades gracefully, and that
nothing spills out of the block (whitespace below the verse block, landscape divider
column integrity, right edge of the weather column). The archived renders
`docs/images/layout_overflow_*.png` are the visual record. (2026-09-12)

## 20. Rounding negative temperatures
`(int)(temp + 0.5f)` is wrong below zero because C truncates toward zero:
`-0.6 → 0`, `-2.5 → -2`. Use `lroundf()` (`cc_roundTemp()`), which rounds half away
from zero. Caught by a hash-equality test: `-0.6` must render identically to `-1.0`.
Relevant for winter mornings in the Melbourne deployment. (2026-09-12)

## 21. Highlight phrases must not be assumed to match
BibleGateway text is HTML-entity-encoded and often re-capitalised (`the Lord` vs
`the LORD`). The old byte-offset `strstr` match silently produced no red accent.
`verseHighlightFound()` (exact, then case-insensitive) lets callers log the miss, and
word colouring now tests *range overlap* rather than "does the phrase start in this
word". (2026-09-12)

## 22. Test suites need invariants, not pixel probes
The original suites asserted 2–4 pixels that happened to be background, so clipping,
overlap and spill all passed. `test_layout_overflow` instead asserts
*properties*: FNV-1a canvas hashes for equivalence (`-0.6` ≡ `-1.0`; typographic UTF-8
≡ its ASCII equivalent), whole-rectangle colour predicates for containment, and a
cell-exact font-signature match for the ellipsis marker. This is what makes 25-second
hardware refreshes safe to defer to the host harness. (2026-09-12)

## 23. Repeat operations are scripted (verify + preview)
Three commands cover almost every iteration; prefer them over ad-hoc commands:
- **`python tools/verify_all.py`** — the whole check in one shot: firmware build
  (`pio run -e s3`, `--clean` for a full rebuild), host suite (cmake configure if
  needed + build + `ctest`), and the render ledger (md5 of `firmware/test/output/*.png`
  against `docs/images/*.png`, reporting missing/stale/orphan files). Exits non-zero on
  any failure, so it is CI-safe. `--fix` resyncs the archive, `--skip-firmware` is the
  fast host-only loop.
- **`python tools/render_preview.py`** — render any verse/weather fixture through the
  real layout engine without flashing: `--orientation`, `--theme`, `--verse`,
  `--verse-file -` (stdin), `--highlight`, `--reference`, `--temp` (negatives fine),
  `--condition`, `--alert`, `--icon`, `--open`. With no `--verse` it uses
  `tools/preview/sample_data.json`, so `--fixture` previews the bundled fixture.
  It drives `layout_render` (`firmware/test/tools/layout_render.cpp`, built by the
  same CMake project) and prints "highlight matched / NOT FOUND" so a silent red-accent
  loss is visible immediately.
- **`python tools/regenerate_screenshots.py`** — build → ctest → archive renders.

Pitfall learned the hard way: `render_preview.py` must write to
`firmware/test/output/previews/`, **not** `firmware/test/output/`, otherwise the
verification ledger treats previews as required renders and the archiver copies
throwaway images into `docs/images/`. (2026-09-12)

## 24. Repository hygiene on this checkout
- Build logs must not be committed: `firmware/build_s3.log` was tracked despite
  `*.log` being gitignored (gitignore does not apply to already-tracked files) —
  removed with `git rm --cached`. `*.log` and `firmware/*.log` are now covered.
- Vendor PDFs stay out of the repo (see lesson 17); datasheet links live in
  `docs/hardware/datasheets/README.md`.
- Git prints `LF will be replaced by CRLF` for edited text files on this machine
  (`core.autocrlf=true` is set): the index stores LF, the working tree CRLF. This is
  expected — don't "fix" line endings or add `.gitattributes` churn for it. What does
  matter is that an individual file uses one ending consistently.
- The host build must not depend on another project's tree: the googletest path is now
  configurable (`-DCHROMAWOTD_GTEST_DIR=<path>`) with a GitHub fetch fallback.
  (2026-09-12)

## 25. One presentation is simpler than five — delete, don't parameterise
The layout engine originally shipped five variants (portrait, portrait-inverted,
landscape, landscape-inverted, landscape-dark) that duplicated the same four zones
with different colours, plus a `drawLayout()` dispatcher whose theme model was
inconsistent (portrait had no "dark"). On 2026-09-12 the product was reduced to a
**single landscape, light presentation**: all portrait/inverted/dark layouts were
deleted, the `Theme`/`Orientation` branching and the `inverted` flags went away, the
colour palette is now plain `CC_*` constants with no switch, and the test count fell
from 19 (4 suites) to 10 (3 suites). This closed REVIEW items D1 (monolith) and D3
(theme inconsistency) by removal.
- Lesson: when a feature matrix is costing more than it earns, deleting the unused
  dimension is better engineering than parameterising it further. A palette object
  would have been the right call while multiple themes existed; with one theme it is
  dead complexity.
- The deletion surfaced two real bugs: `drawOverflowMarker` right-aligned `..` at
  `maxRight - x - w` but tested `>= x - w + 1`, so a marker one pixel short of the gap
  incorrectly degraded to a single `.`; and a single word wider than the 66 px weather
  column overflows its right edge (first word of a line always draws regardless of
  budget). Both fixed; the second is a documented minor limitation for pathological
  words ("thunderstorm" in a 66 px column).
- `tools/render_preview.py` lost `--orientation`/`--theme`, `layout_render` no longer
  takes them, and portrait/inverted/dark PNGs were removed from `docs/images/`. Syncing
  these docs is part of the change, not an afterthought.


## 26. Reuse empty forecast-column space: grow the weather icon when there's no alert

The 66 px right-hand weather column (icon + temp + condition) hugged the top with a
fixed 24 px icon, so a no-alert day left ~45-55 px of dead whitespace at the bottom of
the column. When SPARE, the icon now grows (clamped to 2x = 48 px, shrinking as the
condition wraps more lines) and the icon/temp/condition stack is re-spread to fill the
column; the alert branch is unchanged (icon stays 24 px, alert pinned to the bottom).
Recovers ~30 px of usable bottom space on a 1-line day. Implementation lives in
`drawLandscapeWeatherColumn()` in `firmware/src/verse_display.cpp`; the growth only
happens when `w.alert == null`.
- Before/after renders archived in `docs/images/history/`:
  `layout_landscape_weather_reflow_{before,after}.png` (partly), `..._rain_{before,after}.png`,
  and `..._alert_{before,after}.png` (proves the pinned-alert render is unchanged).
- The weather icons are procedural vector primitives (circles/lines), so growing them is
  an honest re-render, not a bitmap upscale. Two glyphs had hardcoded pixel offsets that
  DID NOT scale with `size` and had to be made proportional before large renders looked
  right: the red rain drops (`cx ± 6/9`, `cy - 4`) and the partly-cloudy sun-stub rays
  (`-3/-1`) now use `size/4`, `size/6`, `size/8` etc.
- Lesson: "use the whitespace" layouts should measure the *wrapped content* height first
  (via `cc_wrappedLineCount`) and give the variable-sized element the leftover; capping
  the growth (`[24, 48]`) prevents a 2-line condition from being crowded out or a
  pathological single word from breaking the column.

## 27. Small proportional font on the panel: mono-hint the TTF, use Roboto 5.5pt

The verse body uses a small (6 pt-class) proportional sans font to get real
descenders (the built-in 5x7 has none). Two things had to be right.

**1. Rasterize with FT_LOAD_TARGET_MONO | FT_LOAD_RENDER, not gray+threshold.**
`tools/font_convert.py` initially loaded glyphs in gray mode (`pixel_mode 2`)
and thresholded at >=128. That produces *muddy, uneven stems* on a 1-bit panel —
it thresholds an anti-aliasing-optimized outline. Adafruit's `fontconvert` (and
every shipped GFX Free Font) uses `FT_LOAD_TARGET_MONO` **combined with
`FT_LOAD_RENDER`**, which yields `pixel_mode 1` — FreeType's native mono
hinting that snaps stems to pixel boundaries. That alone transformed the small
FreeSans from muddy to crisp. (Plain `FT_LOAD_TARGET_MONO` *without*
`FT_LOAD_RENDER` is NOT enough — freetype-py returns a null/empty buffer then,
so the tool silently fell back to the gray path; this was the root cause.)

**2. A small proportional pixel font basically doesn't exist — mono-hint a TTF.**
Tested the genuine pixel-designed families (picotype 5x8, picotypepro 5x10,
picosans 8x16): every one is monospace (uniform advance 7/7/12). At 6-8 px,
hand-crafted bitmap fonts are essentially always fixed-width, because per-glyph
variable widths on a tiny grid is the exception. So "proportional AND
pixel-crisp AND tiny" is a rare combination; the honest answer is a
mono-hinted TTF (Roboto / FreeSans). Roboto reads best of the monospace-hinted
candidates.

**3. Fit and degree-sign.** The verse box is 76 px tall. Roboto 6 pt wraps to
5 lines x 16 = 80 px (overflow 4 px, last line collides the red reference rule);
**Roboto 5.5 pt wraps to 5 x 14 = 70 px** and fits with clearance. The TTF font
is ASCII-only (0x20-0x7E), so `drawString()` silently drops the `°` (0xB0);
the device draw path must special-case `CC_DEGREE` as a vector circle. It must
be drawn relative to the **baseline** (`y + glyphAscent + 2*size`), NOT the
top `y` — the two differ by a cap-height and using `y` mispositions/occludes it.

- **Decision (approved):** Roboto 5.5 pt, mono-hinted, `firmware/src/fonts/Roboto55pt7b.h`.
- **Converter:** `tools/font_convert.py` (DPI 141; `FT_LOAD_TARGET_MONO|FT_LOAD_RENDER`;
  accepts fractional point sizes, name token drops the dot, e.g. 5.5 -> `55`).
- Space between "27°C" and the condition was +1 px (`tempH` 18 -> 19).
- Final render archived: `docs/images/history/layout_landscape_roboto55_final.png`.
- Before/after of the muddy->crisp change:
  `docs/images/history/layout_landscape_freesans6_exact_fixture.png` (muddy,
  gray-threshold) vs `..._roboto55_final.png` (mono-hinted).

## 28. Auto-size the verse body; dedicated temperature/alert fonts

The verse text length varies day to day, so a single body size wastes space on
short verses and overflows on long ones. The body font is now a switchable
pointer (`g_bodyFont`) that defaults to 5.5pt (the main-UI size) but is set
per-verse by `drawVerseBlock`:

- It tries 6pt, then 5.5pt, then 5pt — keeping the first whose wrapped line
  count fits the box (`cc_wrappedLineCount <= cc_lineCapacity`). Short/medium
  verses resolve to 6pt; a boundary-long one steps to 5.5pt; an extreme one
  falls to 5pt (and, if even 5pt overflows, truncates with the ellipsis marker
  as a safe fallback). The original font is restored after the verse draw so
  the header/condition never inherit a non-default size.
- The **temperature** uses a dedicated **10pt** RobotoT font at native size
  (size=1) instead of `setTextSize(2)` on the body font — `setTextSize(2)`
  pixel-doubles and looks chunky/jaggy; a native dot-sized font is smooth.
- The **alert** text is forced to **5pt** via `cc_setBodyFont(&Roboto5pt7b)`
  for the alert block only, then restored.

**Degree sign is font-aware.** It is drawn as a small superscript circle whose
*top* aligns to the font's cap top (`dev_drawDegree`: `cy = base - glyphAscent
+ r`, `r = glyphAscent/4`, clamped 1..3). A fixed `+2*size` offset from the
baseline worked for the short body font but sat ~a cap-height low on the taller
10pt temp font, so the degree looked detached. Shared host+device helper.

**Header reduced.** With the smaller body, the 20px yellow header band read as
oversized; it's now 14px (`headerH`), and the verse box start moves up with it.

**Verse/weather ratio.** The verse area is ~10% wider (splitX 205 -> 226); the
weather column centres itself in the remaining region (`weatherCx` derived from
splitX) so it always sits centred as the ratio changes.

- Body fonts: Roboto 5/5.5/6pt; temp RobotoT 10pt; all mono-hinted
  (`FT_LOAD_TARGET_MONO|FT_LOAD_RENDER`) via `tools/font_convert.py`.
- The GFXfont-dependent helpers (`cc_advanceF`, `cc_glyphAscentF`,
  `cc_measurePxF`, `cc_degreeRadius`, `dev_drawDegree`) and the temp/alert draws
  are guarded by `#ifdef CHROMAWOTD_FONT_FREESANS` so the default 5x7 build
  still compiles (it has no `GFXfont` type and no bundled free font).
- Renders archived: `docs/images/history/layout_landscape_autosize_med.png`
  (medium, 6pt), `..._autosize_4lengths.png` (short/med/long/extralong),
  `..._autosize_boundary55.png` (proves the 6pt->5.5pt step-down).

## 29. Final layout polish (approved)

Fine-grained offsets tuned on-device (see render
`docs/images/history/layout_landscape_layout_tweaks.png`):
- Header title + date drawn at y=2 (was 3) — lifted 1px in the yellow band.
- `FORECAST` label at y=4 and its rule at y=13 — the rule now sits on the same
  line as the yellow header box's bottom rule (`headerH-1`), so the two read as
  one continuous horizontal line across the vertical divider.
- Verse body box margins halved: `drawVerseBlock(4, headerH+4, splitX-8, ...)`
  (was `(8, headerH+8, splitX-16, ...)`) — ~4px padding instead of ~8px.
- Reference line + text moved 5px lower: red rule y=104->109, reference string
  y=110->115. (Sufficient clearance above the 128px panel edge; no clipping.)
- Verbatim note: these are the exact coordinates baked into `drawLayout()` /
  `drawLandscapeWeatherColumn()` — keep them in sync if the split/header change.

## 30. Module split and backend abstraction (REVIEW D1/D6), and why R5 was rejected

The review's core structural finding (D1) was that `verse_display.cpp` was a ~796-line
monolith mixing five concerns, and D6 was that the host/device `#ifdef` backend was
duplicated with the FreeSans glyph rasteriser, degree-sign special-casing and colour
mapping all entangled inside the `dev_*` shims. Both were closed in a single series:

1. **`DisplayTarget` interface (`draw/target.h`)** + two implementations — `SeeedTarget`
   (`target_seeed.cpp`) and `CanvasTarget` (`target_canvas.cpp`). The layout code draws
   through one `target()` reference; colour mapping (device) and glyph rasterisation
   (host bitmap vs device `setFreeFont`+`drawChar`) each live in exactly one target. A
   shared `draw/font_types.h` supplies the `GFXglyph`/`GFXfont`/`PROGMEM` types (host
   shim or device `gfxfont.h`) so no TU re-declares them under `#ifdef`.
2. **Pure text extracted to `text/`** — `glyphs.{h,cpp}` (the single UTF-8 decoder,
   `cc_utf8ToAscii`/`cc_utf8ToAsciiN`) and `wrap.{h,cpp}` (`cc_lineCapacity`,
   `cc_lineBudget`). These carry no font/backend state, so they are directly
   unit-testable (`test_text.cpp`) — which finally closed T1, the last test gap.
3. **Weather icons to `draw/weather_icon.{h,cpp}`**, taking the `DisplayTarget&`
   explicitly rather than reaching for the `dev_*` shims.

**The lesson that mattered: preserve the byte-identical render ledger through a
refactor.** The host harness builds with only `CHROMAWOTD_HOST=1` (5x7 path) while the
device builds with `CHROMAWOTD_FONT_FREANSANS=1` (FreeSans path), so neither `ctest`
alone nor `pio build` alone exercises both. `tools/verify_all.py` md5-compares the host
renders against `docs/images/*.png`; running it after *each* commit is what proved the
backend abstraction changed nothing pixel-for-pixel. The earlier naive D6 attempt
failed because it wired the interface without understanding the FreeSans/degree/colour
entanglement — the correct order was D5 (strip dead decoder) → D6 (interface) → D1
(split), each landed green.

**R5 (comment-style consistency) was rejected, not fixed.** The codebase mixes terse
imperative fragments ("Draw the header band") with full-sentence prose. Re-voicing every
comment to one convention is churn with no behavioural or maintainability payoff — the
high-value comments (buffer boundaries, degree-sign geometry, the 8px inset) are already
present. Rejecting a cosmetic finding outright is a legitimate outcome; it keeps the
check-off table honest instead of leaving an item perpetually "partial".

## 31. Phase 3 network wiring (Open-Meteo + BibleGateway)

The network module (`firmware/src/net/`) provides shared JSON parsing, WMO mapping, and
HTML entity decoding for both host and device. The host uses `curl` via `popen()`; the
device uses `WiFiClientSecure` with pinned root CAs (Amazon Root CA 1 for BibleGateway,
Let's Encrypt YR2 for Open-Meteo).

**Key findings:**
- Open-Meteo returns only WMO codes + temps (no human condition text) — mapped via
  `cc_wmoCondition()` to our 4-icon set + condition strings.
- BibleGateway VotD text is HTML-entity-encoded and may carry a bracketed section heading
  (`"[Final Exhortations]  Rejoice..."`). `cc_stripLeadingBracket()` removes the heading
  while keeping the verse's own opening/closing quotes (the heading sits *inside* them).
- The JSON parser must skip mismatched-type values: `current_units.temperature_2m` is the
  string `"°C"`, not the numeric `current.temperature_2m`. `parseMember()` scans forward on
  a type mismatch so the numeric `current` member wins.
- **TLS stack overflow, and why `sdkconfig` overrides don't work here.** The default ~8 KB
  `loopTask` stack overflows partway through `WiFiClientSecure::connect()` (`Guru Meditation
  Error: Stack canary watchpoint triggered (loopTask)`, backtrace through
  `mbedtls_entropy_func` → `ctr_drbg_seed` → `start_ssl_client`). Neither
  `-DCONFIG_MAIN_TASK_STACK_SIZE=...` in `build_flags` nor
  `board_build.sdkconfig_flags = CONFIG_...` helps: the former is silently shadowed, the
  latter breaks the link (`cannot find -lboard_build.sdkconfig_flags`). This project uses the
  **precompiled** Arduino-ESP32 core, not an IDF component build, so `sdkconfig.h` is baked
  into the framework package and its task-stack sizes cannot be changed from
  `platformio.ini`. **Fix:** run the Wi-Fi + TLS Sync phase on a dedicated FreeRTOS task
  with an explicit stack (`xTaskCreatePinnedToCore(syncTask, "cc_sync", 16384, ...)`), with
  `setup()` blocking on a binary semaphore until it signals done. Portable, no framework
  config needed. See `main.cpp`'s STACK NOTE.
- **Empty-body `InvalidInput` despite `HTTP 200`.** A byte-at-a-time
  `WiFiClient::available()`/`read()` loop exits early — `available()` can be transiently
  false before all TCP segments have arrived, so the buffer ended up empty and
  `deserializeJson()` reported `InvalidInput`. Use `HTTPClient::getString()`, which blocks
  until the whole body is read (respects `Content-Length`/chunked). See `fetchHttpGet()`.

**Verified on hardware (XIAO ESP32-S3 / EE05):** Wi-Fi connects, both fetches succeed
(`sync: verse OK (Philippians 4:4)`, `sync: weather OK (16.4 C, Partly cloudy)`), and the
layout renders + refreshes with no crash. The VotD source is deterministic: the URL
hardcodes `&version=NIV`, so every fetch (host and device) requests the same translation
explicitly rather than relying on a server-side default.

**Tests:** `test_net.cpp` (15) covers WMO mapping, HTML decode, section-heading strip, and
live fetch. `layout_render --live` renders real data through the layout engine.

(2026-09-12)

## 32. Phase 4 — deep sleep, wake schedule, and the USB-port-meets-deep-sleep trap

Refresh schedule: three fixed local slots — **06:30 / 12:30 / 18:00** — with the device
asleep in between (so ~3 full sweeps/day instead of continuous refresh). The arithmetic is a
pure module (`firmware/src/sched/wake_schedule.{h,cpp}`, `cc_secondsUntilNextWake`) so it is
unit-tested (`test_sched.cpp`, 8 tests incl. midnight roll-over and "never returns 0").

- **Wall time survives deep sleep.** The ESP32-S3 RTC domain keeps the system clock across
  deep sleep, so after one NTP sync the next wake already knows the time; each wake
  re-syncs anyway (`configTzTime` + a bounded `getLocalTime(…, 6000)`). If the clock is not
  yet valid (e.g. first boot with no Wi-Fi), the device sleeps a 1 h fallback rather than
  trusting a bogus "next slot". A 60 s floor prevents a wake loop.
- **Deep sleep makes the native USB port disappear.** The XIAO ESP32-S3 has no USB-UART
  bridge — it uses the ESP32-S3's built-in USB Serial/JTAG, which is in the digital domain
  and is powered down in deep sleep. Consequence: the COM port vanishes the moment the
  device sleeps, so `pio run -t upload` can only succeed during the brief awake window.
  Budget for this when iterating on a deep-sleeping device.
  **"Hold BOOT, tap RESET" is the RECOVERY path, not the routine one** (corrected
  2026-09-13 after the user pointed out that a double-tap of RESET suffices in practice).
  The native USB Serial/JTAG peripheral can trigger download mode *itself*, so
  re-enumerating the port — double-tap RESET, or unplug/replug USB — is normally all that
  is needed and a plain upload then succeeds; this was demonstrated in-session on
  2026-09-13 (a `-t upload` succeeded with no button sequence at all). Keep the
  BOOT/RESET sequence for a wedged chip or a port that will not enumerate, e.g. left in
  download mode by a bad DTR/RTS reset (LESSONS §9). The BOOT and RESET buttons are on the
  XIAO module beside the USB-C connector and are tiny; the EE05 carrier's `1x Reset, 3x
  User` buttons are a different set (GPIO2/3/8 — see §33). Prefer
  `tools/flash_when_awake.py`, which polls for the port and uploads on sight.
- **Bring-up helper.** `-DCHROMAWOTD_WAKE_TEST_SEC=<n>` forces a short sleep so timer-wake
  can be observed on the bench without waiting hours; `-DCHROMAWOTD_DEBUG_DELAY=1` adds a
  3 s post-boot delay for monitor attach. Neither is set in production builds.
- **Confirmed on hardware.** Entry: the device logs `sleep: now 21:11:42 -> next wake in
  33498 s (9.30 h)` (exactly the next 06:30) then `esp_deep_sleep_start()` runs to
  completion. Wake: with a temporary `-DCHROMAWOTD_WAKE_TEST_SEC=90`, polling for COM-port
  *presence* (never opening it) showed the exact cycle — `21:06:36 AWAKE → 21:07:06 ASLEEP
  → 21:08:36 AWAKE` — i.e. ~30 s awake (sync+refresh) then a 90 s sleep gap, repeating
  unattended. Since the timer is the only configured wake source, that cycling *is* the
  proof of RTC-timer wake.
- **Caveat: opening the serial monitor resets the ESP32-S3.** `pio device monitor` asserts
  DTR/RTS on open, which reboots the chip (same DTR/RTS class of effect as LESSONS §9), so
  every monitor attach produced a fresh `wake_cause=0 (power-on/reset)` cycle and masked the
  real timer wake. To observe a deep-sleeping board without perturbing it, poll *port
  presence* (`serial.tools.list_ports`) rather than opening the port; the firmware also
  re-prints `awake-status: wake_cause=N` immediately before sleeping so a late-attaching
  monitor can still see how the current cycle was triggered.

(2026-09-12)

## 33. Phase 5 — buttons, time-based Verse/Word switching, and the Word-of-the-Day source

**Buttons (EE05).** The real pin map was established on hardware with the `env:probe`
diagnostic (an infinite GPIO watch), **not** from the schematic — reading the schematic's
XIAO symbol with `pdftotext -layout` scrambles the net-label-to-pin association, and the
first reading put BUTTON3 on D4 (which is actually `I2C_SDA` and caused a deep-sleep wake
storm; see §34). The verified map:

| Button | XIAO pin | GPIO | Polarity |
|--------|----------|------|----------|
| BUTTON1 | D1 | GPIO2 | active-low |
| BUTTON2 | D2 | GPIO3 | active-low |
| BUTTON3 | D9 | GPIO8 | active-low |

**D0/GPIO1 is `BAT_ADC`** (battery-sense divider), *not* a button. All three button pads are
RTC-capable, so one shared `ext1` (all-low) mask wakes the chip, and a press runs the same
Sync→Render→Sleep path as a timer wake (`wake_cause == ESP_SLEEP_WAKEUP_EXT1`). Verified on
hardware: press → `wake cause: 3 (button)` → full sync → refresh → sleep to the next slot
(29579 s).

**Time-based content.** `sched/content_policy.{h,cpp}` is pure + unit-tested:
`cc_contentModeForHour()` (Verse before noon, Word from noon) and `cc_useTomorrowForecast()`
(18:00 onwards). Both are also applied to button-triggered syncs, and if NTP failed the
device falls back to Verse/today rather than showing the wrong thing.

**Word-of-the-Day source — what worked and what didn't.** Seeking a keyless feed:
- Merriam-Webster's WOTD RSS returns **403 behind Cloudflare** ("Just a moment…") — an
  ESP32 can never pass that challenge. Dead end.
- `wordsmith.org/words/rss.xml` and friends **404** — but the daily HTML page
  `https://wordsmith.org/words/today.html` returns 200 and, crucially, carries a
  **respell pronunciation** `(bre-VIL-uh-kwuhnt)`. That is plain ASCII, so no IPA font work
  is needed on a panel whose font is ASCII-only. (A keyless JSON alternative,
  `tinymind.eu/api/word.php`, was rejected precisely because it only offers IPA, which
  renders as `?`s.)
- Licensing: A.Word.A.Day content is © Wordsmith.org; it is used here for a personal
  device and is **not** redistributed in the repo. A bundled fallback word covers fetch
  failure.

**A.Word.A.Day parser pitfalls** (`cc_parseAwad`, shared host + device):
- Every section is `<div style=…>LABEL:</div>\n<div …>\nVALUE\n</div>`. The value search
  must **skip the label's own `</div>` first** — stopping at the first `</div>` after the
  label yields an empty value (this failed all parser tests until fixed).
- USAGE carries a `<br>` + attribution after the quote, so the example is cut at the
  closing curly quote (`&#8221;`); the attribution must not leak into the body.
- **Whitespace is a real bug source.** The source HTML wraps values across lines; those
  newlines are ASCII (< 0x80) so they sail past an "is it ASCII?" check but reach the glyph
  rasteriser as control characters and render as garbage (`you do?'?shrug`). Normalise to
  single spaces in the extractor and assert **printable** ASCII (0x20–0x7E) in tests — the
  weaker `< 0x80` assertion would not have caught it.
- `wordsmith.org` uses the same Let's Encrypt **YE1** (ECDSA) intermediate as tinymind, so
  it is pinned alongside Amazon Root CA 1 (BibleGateway) and YR2 (Open-Meteo).

**Layout.** The bottom rule now carries two optional captions: a black one at the left (the
pronunciation respelling) and the red one at the right (the verse reference, or the WOD
headword). The right caption is placed first and the left is only drawn if it still clears
it, so a long word + long respelling degrades by dropping the pronunciation instead of
overprinting. Presentation strings moved into a `LayoutOptions` struct (headerTitle /
weatherLabel / leftCaption) so adding a label is one field, not another positional argument.

> **Superseded 2026-09-22 (§65):** dropping the pronunciation was the wrong degrade — it fired for
> 3 of the 18 most recent words, and the respelling is the only reason this source was chosen. The
> respelling now shrinks (7 → 5pt) to fit and is truncated visibly as a last resort. (The dual
> caption itself also moved into the header band in the 2026-09-19 verse-first layout.)

(2026-09-12)

## 34. Button wake: GPIO5/D4 was not a button — arming it caused a deep-sleep wake storm (RESOLVED)

**Symptom.** With `ext1` armed on GPIO2/3/5 (`ESP_EXT1_WAKEUP_ALL_LOW`), the device entered a
wake storm: boot → sync (~13 s) → sleep → wake ~1 s later → repeat, forever. Measured
non-invasively by polling COM-port presence (~14 s cycles).

**Root cause.** The pin map was taken from a `pdftotext -layout` reading of the EE05
schematic, where the XIAO symbol's net labels do not preserve their association with the pin
numbers. It gave `BUTTON3 = D4 = GPIO5`. An on-hardware probe (`env:probe`) showed the truth:

| Button | XIAO pin | GPIO | Polarity |
|--------|----------|------|----------|
| BUTTON1 | D1 | GPIO2 | active-low |
| BUTTON2 | D2 | GPIO3 | active-low |
| BUTTON3 | **D9** | **GPIO8** | active-low |

**GPIO5/D4 is not a button at all** — the schematic labels it `I2C_SDA`. It happens to idle
HIGH (so it looked plausible), but it does not stay high once the chip is in deep sleep, and
a level-triggered `ALL_LOW` wake fires immediately when any armed pin is low. Arming a
non-button pad was the entire storm.

**Fix.** Arm `ext1` on GPIO2/3/8 only, plus: `rtc_gpio_init()` +
`RTC_GPIO_MODE_INPUT_ONLY` + `rtc_gpio_pullup_en()`/`pulldown_dis()` before sleeping, read
levels with `rtc_gpio_get_level()` (never `pinMode()`, which hands the pad back to the digital
domain and undoes the RTC pull-up), and arm only pins that idle HIGH. Verified on hardware:
`wake cause: 3 (button)` → full sync → refresh → `sleeping 29579 s`, no storm.

**Lesson.** Do not derive wake-pin maps from a schematic you had to decode with `-layout`;
probe the pins on the real board. The `env:probe` diagnostic (an infinite GPIO watch that
prints level transitions) settled in one flash what two deep-sleep iterations could not.

**Method note.** `pio device monitor` asserts DTR/RTS on attach, which **resets** the
ESP32-S3. A retry-loop of monitor attaches therefore *manufactures* a ~14 s reboot cycle that
is easily mistaken for a firmware wake storm — the first storm "measured" that way was partly
self-inflicted. For wake/sleep work use either the port-presence poll (never opening the port)
or a pyserial reader that sets `dtr=False`/`rts=False` before `open()`.

(2026-09-12)

## 35. Drizzle (and Rain 61-67) draw the plain Cloud icon — RESOLVED 2026-09-19

> **Resolved 2026-09-19 (LESSONS §61):** the weather ICON was removed from the display —
> the condition is TEXT now — so this limitation no longer exists; "Drizzle" and "Rain" are
> spelled out. The diagnosis below is kept because the mapping rule it describes
> (`cc_wmoCondition`) is unchanged and remains the single source of truth for the words.

**Symptom.** A "Drizzle" forecast shows the generic outline cloud, not the rain icon.

**Cause.** Only WMO **≥ 80** (rain showers) map to `WeatherIcon::Rain`; everything at/above 45
falls through to `WeatherIcon::Cloud`:

- Host: `firmware/src/net/net.cpp` `iconFromWmo()` — `if (code >= 45) return WeatherIcon::Cloud;`
- Device: `firmware/src/net/net_impl_esp32.cpp` (~line 207) repeats the same table inline.

Both mappers also send **WMO 61/63/65/66/67 ("Rain")** to Cloud, and 71-77 Snow / 45-48 Fog to
Cloud. The condition *text* is correct in every case (`cc_wmoCondition()`); only the icon is
coarse. The icon set is deliberately 4 wide (Sun, Cloud, Rain, PartlyCloudy) — there is no
drizzle/snow/fog glyph.

Drizzle → Cloud vs Drizzle → Rain:
`docs/images/history/weather_icon_drizzle_current_vs_rain.png`

**DECIDED (2026-09-12): leave the mapping as-is.** Accepted as a known limitation: the text
label carries the precision, the icon is a coarse weather cue, and re-map/re-designing icons was
not worth a panel refresh round. Do NOT "fix" it silently — check here first.

**If it is ever revisited**, two things change together: (1) route 51-57 *and* 61-67 to
`WeatherIcon::Rain` (fixing the text-says-Rain/icon-says-cloud case), and (2) export the mapper
as a shared `cc_wmoIcon()` from `net.cpp` so the device stops duplicating the table — the two
copies can drift today because `iconFromWmo()` is `static`.

(2026-09-12)

## 36. Layout alignment must be measured on INK, not on the pen origin

Two alignment defects were reported from the panel and both turned out to be
measurable facts rather than taste:

1. **The weather-column caption (`FORECAST` / `TOMORROW`) was drawn at y=4 while the
   header title and date are drawn at y=2.** Its ink therefore occupied rows 4–10 and
   sat only 2 px above the rule at y=13 — reading as "touching the line". Drawn at
   y=2 instead, its ink occupies rows 2–8: level with the date, 4 px clear of the rule.
   A caption and the date it sits beside should share a y, not merely look "close".

2. **The caption rule and the pronunciation caption started at x=10, but the body
   text block starts at `kVerseMargin` = 4.** A 6 px offset. The rule now spans
   `kVerseMargin .. kSplitX - kVerseMargin` (x 4–222), matching the body block's
   declared margins on both edges, and the left caption shares that margin.

**Measure ink, not the drawing coordinate.** After moving the caption to x=4 its ink
still began at x=5, because `'('` carries a 1 px left side bearing — and the caption
always starts with `'(‘` in A.Word.A.Day's format. A 1 px nudge compensates, so the
caption's *ink* lands on x=4 with the rule and body text. Eyeballing a 296×128 PNG
cannot distinguish a 1 px bearing from a rounding error; the pixel scan can.

**Tooling (added because this scan was written by hand three times):**
- `tools/measure_layout.py` — reports and asserts the geometry invariants above
  (header-level, rule extent, body margin, caption margin, caption-below-rule) on
  any render, and is wired into `verify_all.py` as the alignment stage (5/5), so an
  alignment regression now fails the one-command check. It skips cleanly if Pillow is absent
  (not a hard project dependency).
- `tools/bench_watch.py` — the bench observer: `--presence` polls the serial port
  without opening it (the only trustworthy wake/sleep trace, see §34), and
  `--capture` waits for the device to wake and reads the boot log with DTR/RTS
  deasserted so it does not reset the board.

(2026-09-12)

## 37. Weather policy: expected daily maximum instead of instantaneous temperature

**Change & Rationale.**
Previously, daytime syncs (< 18:00) extracted `current.temperature_2m` and `current.weather_code`. On an ePaper device refreshing only at 06:30 and 12:30, displaying the instantaneous temperature at 06:30 (e.g. 8°C) is unhelpful for planning the day when the forecast high is 22°C.

The policy now consistently presents daily forecast expectations:
- **00:00–17:59 (Today / `FORECAST`)**:
  - Maximum temperature: `daily.temperature_2m_max[0]`
  - Expected weather condition & icon: `daily.weather_code[0]`
- **18:00–23:59 (Tomorrow / `TOMORROW`)**:
  - Maximum temperature: `daily.temperature_2m_max[1]`
  - Expected weather condition & icon: `daily.weather_code[1]`

**Network & Parser Optimization.**
- The Open-Meteo URL query dropped `&current=temperature_2m,weather_code` completely, requesting only `&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=2`. This reduces the JSON payload size and removes any ambiguity between current vs daily members.
- Both host (`net.cpp` using `parseArrayNumber`) and device (`net_impl_esp32.cpp` using `ArduinoJson`) index into `daily.temperature_2m_max[dayIdx]` and `daily.weather_code[dayIdx]`, where `dayIdx = tomorrow ? 1 : 0`.
- Verified live on hardware: morning sync fetched 22.9°C (today's high) and "Drizzle" (today's forecast code), matching daytime expectations.

(2026-09-13)



## 38. The verse font auto-size ladder was never compiled by the host suite (RESOLVED)

**Symptom.** Looking at a today's-message render, the verse body appeared in the
smaller body font even though the verse is short. The device build has
`-DCHROMAWOTD_FONT_FREESANS=1` in `platformio.ini`, so the auto-size ladder
(`Roboto 6pt -> 5.5pt -> 5pt`, largest that fits) *is* active on the panel.

**Root cause of the local blind spot.** Every host target in
`firmware/test/CMakeLists.txt` defined only `CHROMAWOTD_HOST=1`. The ladder lives
under `#ifdef CHROMAWOTD_FONT_FREESANS`, so ctest, `layout_render`,
`regenerate_screenshots.py` and the whole `docs/images/` ledger exercised the
**non-FreeSans** path only — the one code path the panel actually runs had zero
local coverage, and the committed renders are 5x7 bitmap font, not the shipped
proportional font. A green `verify_all.py` therefore said nothing about it.

**Fix.** Extract the selection into `cc_pickVerseFont()` / `cc_verseFontSize()`
(`verse_display.h` exposes the enum, the ladder stays static), move
`kVerseMaxW`/`kVerseMaxH` into the header as the single source of truth shared by
`drawLayout()` and the tests, and add a `test_verse_autosize` target that CMake
compiles **with** `CHROMAWOTD_FONT_FREESANS=1` so the ladder is covered.

**Refactor trap (the actual bug found).** The wrap-count measurement routes
through `cc_advance()`, which reads the global `g_bodyFont`. The original loop
assigned `g_bodyFont = candidates[i]` *before* measuring, which is load-bearing:
measuring every candidate while one font is active makes the line count
font-independent and collapses the ladder (a 193-char verse then wrongly selected
6pt). Any extraction must keep setting the candidate as the active body font for
its own measurement, and restore the previous font afterwards. Assert the
*monotonic* property (longer text never picks a larger font) plus a verified
concrete case per rung, not just the happy path.

**Verified.** Live 2026-09-13 VOTD (`"Rejoice in the Lord always. I will say it
again: Rejoice!"`, Philippians 4:4, NIV) selects **6pt**; 191-char Psalm 23
excerpt selects 5.5pt; a 316-char Galatians excerpt bottoms out at 5pt; empty
text selects 6pt. Render archived as
`docs/images/history/layout_landscape_verse_autosize_6pt_rejoice.png`.

(2026-09-13)


## 39. Font-specific test calibration: the same ink means different things per font (RESOLVED)

**Context.** Adding `-DCHROMAWOTD_DEVICE_FONTS=ON` (`firmware/test/CMakeLists.txt`) to run the
host suite on the shipped proportional font path immediately failed two layout invariants that
had been passing since they were written — i.e. never once exercised against the shipped font.
Both were **test calibration**, not product defects — but only a render proved it, and the
distinction matters:

1. `WeatherAlert.TruncatedAlertStaysInsideColumn` and
   `VerseOverflow.LandscapeMarksOverflowAndKeepsDividerIntact` looked for the `...` overflow
   marker with `cellIsDotGlyph()`, a signature hardcoded to the **5x7** `'.'` (2x2 ink at
   columns 2..3, rows 5..6 of a 6x8 cell, 6 px pitch). In Roboto `'.'` is a **1x1 dot** with a
   3 px advance, so the detector could never match. Measured alternative: isolated single-pixel
   dots 3 px apart.
2. `LayoutAlert.LandscapeWithAlert` probed the single pixel `(251, 94)` for the red alert
   divider. `drawLandscapeWeatherColumn()` computes `divY = labelY - 3` from the **wrapped line
   count** of the alert text, so fewer lines in the proportional font pin the rule higher —
   measured **y=101** on the device path vs **y=94** on 5x7, with the rule and the red
   `ALERT:`/text present in both. The test had encoded one font's wrap count as geometry.

**Rules.**
- **Never hardcode a glyph's ink shape or a font-dependent coordinate in an invariant.** Detect
  the marker/feature by a property that survives the font (a run length, an isolation test, a
  colour predicate over a region) and assert the RELATION (rule below the temperature, above the
  alert text), not one pixel's address. This is the third time a pixel-probe habit has had to be
  corrected: `§22` (probes asserting background pixels, so clipping and spill passed), `§36`
  (alignment judged on the pen origin instead of the ink), now `§39`.
- **A dot detector needs isolation on all four sides.** Requiring clean pixels above/below admits
  the END of any baseline-terminating stroke: the bottom of `s`/`u`/`n` presents a last pixel
  that is dot-shaped with clean above/below, and at Roboto's 3 px advance it lands exactly 3 px
  from its neighbour — a false `...`. Adding left/right isolation rejects those horizontal runs.
  Verify a detector against a KNOWN-NEGATIVE render (a short verse that fits) before trusting it.
- **Mind the region bounds when a marker sits near an edge.** The alert's `...` lands on the
  panel's second-to-last row, so a search loop bounded by `y + 2 <= y1` skips it. `CcCanvas::
  getPixel()` is bounds-safe, so the neighbour reads may fall outside the region — only the dot's
  own row must be inside it. `rectAllColor(227, 0, 230, ...)` also over-claimed: x=230 is not
  gutter, it carries the centred condition line once the proportional font stops wrapping it.
- **Run the suite on the font path you ship.** Both failures were invisible because the flag that
  selects the shipped font never reached the host build; `verify_all.py` now runs the device-font
  suite as stage 3/5, then re-runs the canonical suite so the ledger still compares 5x7 renders
  (the layout tests write to fixed `output/` paths and would otherwise clobber the baseline).

**Tools.** `python tools/font_size_probe.py --live` answers "which body font did today's content
get?" by calling `cc_verseFontSize()` through `layout_render --live` — the answer comes from the
real engine, never a re-derivation.

(2026-09-13)


## 40. Write lessons that the repository can substantiate (RESOLVED)

**What happened.** LESSONS 39 originally said two invariants "had passed for months". The
repository's first commit is 2026-09-11 and the lesson is dated 2026-09-13, so no test in it
could have passed for months. The claim was invented framing, and it made the finding *worse*:
the plain fact — **the tests had never once run against the shipped font** — is both true and
the reason the failure was worth recording.

**Rules.**
- **Anchor every claim to something checkable**: a commit date (`git log --reverse`), a measured
  value (y=101 vs y=94), a tool's output, a file's line count. If a claim needs a timeframe,
  derive it from the history instead of gesturing at one.
- **Do not inflate a finding's surprise.** "Never exercised" is a stronger and more useful
  statement than "passed for months until now"; the second implies prior regression testing that
  did not happen.
- **Re-check cross-references before citing them.** Two consecutive lessons cited `§36` for the
  pixel-probe precedent when it is `§22` (probes asserting background pixels), and called this the
  second occurrence when it is the third. Grep the referenced section and quote what it actually
  says.
- **Renumbering a multi-stage tool leaves stale references behind.** Moving `verify_all.py` from
  4 to 5 stages left "stage 4/4" and "Four stages" in README and an earlier lesson. When a count
  in one artifact defines a count elsewhere, grep the whole repo for the old number.

(2026-09-13)


## 41. Secret scanning was absent, not just unconfigured (RESOLVED)

**Audit result (2026-09-13): no secrets leaked.** `git log --all` was scanned over the full
history — 79 commits, no leaks. In particular `firmware/src/secrets.h` (which DOES hold live
Wi-Fi credentials on this machine) has never been committed: it is gitignored, and the only
tracked variant is `secrets.h.example`, whose values are placeholders. No private keys, no
`.env`, no `.pem`/`.key` in any revision.

**The gap was the guard, not the history.** `secrets.h` was the ONLY entry in `.gitignore` for
credentials, and there was no pre-commit hook, no gitleaks config, and no scan in CI — unlike the
sibling eClock project, which has all three. One stray `git add -f` or a differently-named file
(`.env`, `secrets.ini`) would have committed a credential with nothing to catch it. Ported eClock's
protections:

- `.gitleaks.toml` — default rule set + an allowlist for vendored/build content (`.pio/`, CMake
  build trees, `firmware/test/output/`, `firmware/test/third_party/`, the vendored font tables).
  Authored source is deliberately NOT allowlisted, so a real secret there still fails.
- `.pre-commit-config.yaml` — gitleaks plus hygiene hooks (`trailing-whitespace`,
  `end-of-file-fixer`, `check-yaml`, `check-added-large-files`, `check-merge-conflict`,
  `detect-private-key`). Installed via `pre-commit install`.
- `.gitignore` — added `secrets.ini`, `.env`, `.env.*`, `*.pem`, `*.key`, `.venv/`, `venv/`.
- `.gitattributes` — `* text=auto` plus binary rules, so the CRLF/LF churn stops (`docs/images/*.png`
  is marked binary because `verify_all.py` md5-compares those renders byte-for-byte).
- CI job `secrets` — `gitleaks detect --source . --config .gitleaks.toml --redact --exit-code 1`
  with `fetch-depth: 0`, so a push re-scans ALL history, not just the pushed tip.

**Verify a detector, don't assume it.** A scanner that never fires is indistinguishable from a
scanner that cannot fire. Both directions were tested with a deliberately planted (fake) AWS key:
`gitleaks detect` reported `leaks found: 1` with exit code 1, and the installed pre-commit hook
returned exit code 1, blocking the commit. Then the file was removed.

**Pitfalls.**
- `.clang-format` is YAML-ish but starts with `#` comments and a bare `Language:` preamble that
  PyYAML rejects, so `check-yaml` fails the whole run. eClock never hit this because it has no
  `.clang-format`; the fix is `exclude: ^\.clang-format$` on that hook — exclude the file, never
  delete the check.
- The hygiene hooks rewrote 27 files on the first `--all-files` run (missing trailing newlines and
  trailing whitespace). That is expected on a fresh adoption — but re-run the full suite afterwards,
  because the rewrite touches firmware source: `verify_all.py` came back ALL GREEN after it.

(2026-09-13)


## 42. CI had never run — publishing it exposed four pre-existing breakages (RESOLVED)

**Context.** The repo was pushed to `github.com/JPTranter/ChromaWOTD` on 2026-09-13. Until then
there was no remote, so `.github/workflows/ci.yml` (committed 2026-09-12) had **never executed**.
The first run failed in every job except the newly added secret scan. None of the failures were
caused by publishing; publishing is what made them visible. All four are now fixed and CI is green.

1. **The firmware could not build from a fresh clone.** `net_impl_esp32.cpp` did an unguarded
   `#include "secrets.h"`, but that file is gitignored, so a clean checkout has only
   `secrets.h.example` and the build died with `secrets.h: No such file or directory`. The file
   already intended to degrade gracefully (`cc_wifiConnect()` returns early when `WIFI_SSID` is
   absent) but the include made that unreachable, and `CHROMAWOTD_LATITUDE/LONGITUDE/TIMEZONE`
   had no defaults. Fix: `__has_include("secrets.h")`, falling back to the example, plus
   `#ifndef` defaults. This is the most serious of the four — it means no new contributor could
   ever have built the project either.
2. **`.clang-format` used pre-v18 lowercase enum values** (`AlignEscapedNewlines: left`,
   `AlignOperands: right`, `AlwaysBreakAfterReturnType: none`, `AlwaysBreakTemplateDeclarations:
   yes`, `BreakConstructorInitializers: beforeColon`, `PointerAlignment: left`). Current
   clang-format rejects these outright, so the whole config failed to parse and the lint job
   errored before checking any source. Sweep them individually rather than fixing one per run —
   a loop over each key against `clang-format --dry-run` finds all of them at once.
3. **The lint gate was not reproducible.** The runner used apt's clang-format, a different version
   from the local one, and reported violations in code that was already formatted. Pin the
   formatter (`pip install clang-format==23.1.1`) so CI and local agree.
4. **`weather_icon_sheet.png` was a ledger orphan.** It is produced only by
   `tools/weather_icon_sheet.py`, is referenced by no documentation, and that tool's own docstring
   says its output belongs *outside* the `docs/images` ledger — but an earlier "refresh ledger"
   commit swept it in. No test can regenerate it, so the ledger was right to flag it (it would
   silently rot). Moved to `docs/images/history/`.

**Rules.**
- **A CI workflow that has never run is not evidence of anything.** Treat the first real run as a
  test of the workflow itself, and fix what it finds rather than assuming the config was correct
  because it was committed.
- **Anything a test asserts must be reproducible from a clean checkout with no untracked files.**
  `secrets.h` is the exception that proves it: if CI cannot build without a file, the project
  cannot be built by anyone but the author.
- **Pin tool versions that a check depends on.** A formatter/linter whose version floats will
  disagree between environments and the gate becomes noise.
- **Scope file lists with `:(glob)` pathspecs, not shell globs.** Two separate traps: a shell
  glob `firmware/src/*.h` matches the gitignored `secrets.h` (so local and CI disagree, and
  clang-format prints real credentials to the terminal — which happened), while git's pathspec
  `*` matches across `/`, so `'firmware/src/*.cpp'` recurses into subdirectories and drags in the
  auto-generated font tables. `git ls-files ':(glob)firmware/src/*.cpp'` gives the intended scope.
- **Keep generated/tool-only artifacts out of the ledger** (`docs/images` root). The ledger's
  md5 equality check is only meaningful for renders a test produces.

(2026-09-13)


## 43. Configuration was compile-time only — and the two copies of its defaults had already diverged (RESOLVED)

**Context.** "How does a user change their settings, or keep the settings already on the
device?" had no good answer: every value (SSID, passphrase, lat/lon, timezone) was a
compile-time `#define` from the gitignored `secrets.h`, so nothing was stored on the device,
and README/ARCHITECTURE described NVS + a captive portal that did not exist. Grepping for
`Preferences`/`nvs_get` in `firmware/src/` returned nothing — the docs were aspirational
(Phase 2 of PROJECT_PLAN), which is the same doc-vs-reality gap as the BOOT/RESET claims (§32).

**Latent bug found while consolidating.** The fallback defaults were duplicated in
`net_impl_esp32.cpp` (device) and `net.cpp` (host) and had **drifted**: Sydney (-33.8688,
151.2093) vs Melbourne (-37.8528, 145.1633). So the host harness and the firmware could
compile *different* coordinates, and the project's "host and device stay identical" invariant
was quietly false for configuration — the same class of divergence the layout engine is
carefully built to avoid. Now defined once in `config/config_compiletime.h`.

**Design.** Resolution order NVS → `secrets.h` → built-in default, one shared instance
(`config_active.cpp`). This is what makes partial updates work without a flag: NVS lives at
`0x9000` and an **app-only** flash writes from `0x10000`, so settings survive an update, while
the merged image at `0x0` (which spans the NVS region — verified: 20 KB, all `0xFF`) yields a
factory-fresh device. The choice of release artifact *is* the choice of behaviour.

**Rules.**
- **Put configuration behind one resolution rule and one instance.** Two places deriving the
  same defaults WILL drift; the duplication survived because nothing compared them.
- **A gesture that spans deep sleep cannot be timed with `millis()`.** A "hold for 10 s" reset
  cannot know how long the user held before the boot, because the press *is* the wake source.
  Sample the level repeatedly instead (pure classifier in `sched/hold_gesture.cpp`, so it is
  testable without hardware). Read such buttons through the RTC domain (`rtc_gpio_*`), never
  `pinMode`/`digitalRead`, which hands the pad back to the digital domain (§34).
- **Generator functions should take their entropy as an argument.** `cc_portalMakeInfo()` is
  pure (MAC + seed → name/password), so the credential rule is unit-tested on the host while
  the device supplies `esp_random()`. The password alphabet excludes O/0/I/1/L because a human
  transcribes it from a 4-colour panel by eye.
- **Bound and validate everything from a web form** — that is remote input (S4). Values are
  length-clamped, HTML-escaped when echoed back, coordinates are range-checked, and `(0, 0)` is
  rejected explicitly rather than silently producing a plausible-but-wrong forecast.

**Not yet verified on hardware.** The portal compiles, boots to the setup screen and the
config layer's precedence is exercised end-to-end on the host, but no one has joined the AP
from a phone. Treat the captive-portal sheet behaviour (DNS catch-all, OS sign-in sheet) as
unproven until tested on the real device.

(2026-09-13)


## 44. The first hardware boot log found two bugs no host test could (RESOLVED)

**Context.** The portal was configured end-to-end on the real device for the first time
(scan/submit → NVS write → reboot → join the home network → render). Reading that boot log
found two defects that every host test and every render had passed over.

1. **Every weather fetch was failing with HTTP 400.** The URL sent the configured timezone as
   `timezone=`, but the device stores a POSIX TZ string (`AEST-10AEDT,M10.1.0,M4.1.0/3`)
   because that is what the local clock needs, and Open-Meteo expects an IANA name. Measured:
   `AEST-10AEDT` → 400, `AEST-10AEDT,M10.1.0,M4.1.0/3` → 400, `Australia/Melbourne` → 200,
   `auto` → 200. Two different needs were sharing one field. The API now always asks for
   `timezone=auto` (it derives the zone from the lat/lon already sent) and the POSIX string
   serves only the local clock.
2. **Why it hid for the whole session:** the live-fetch tests `GTEST_SKIP()`-ed on any failure.
   A permanent HTTP 400 looked exactly like "no network", so the suite stayed green while the
   weather column was empty. They now probe the endpoint and FAIL — not skip — when it is
   reachable but the request is malformed. Verified both ways: with the bug reintroduced the
   test fails naming the cause; with the fix it passes.

**Lessons.**
- **A skip-on-failure test cannot distinguish "offline" from "broken".** If a test skips
  whenever the thing under test fails, it can never report that thing being broken. Probe
  reachability separately and fail on the difference.
- **Check your probe's failure mode.** The first version of the reachability probe put a query
  string in the `system()` call; `cmd.exe` splits on `&`, so it reported "offline" while the
  endpoint was fine — the probe silently disabled the very check it existed to enable. Probe
  URLs must contain no shell metacharacters.
- **Retry before declaring a defect, but not before declaring a bug.** A transient timeout is
  not a malformed request. One retry separates them: a real HTTP 400 fails deterministically
  on both attempts, a slow link often succeeds on the second.
- **Some properties are only observable on hardware.** Host tests and renders cannot see a
  remote API rejecting a request, and they cannot see a config value that the portal wrote.
  The boot log from a real device is a first-class test artifact.

(2026-09-13)


## 45. Stored config vanished: the Arduino core silently reformats NVS when it fills (DIAGNOSED)

**Symptom.** After the portal had been configured successfully on hardware (the device
joined the home network and rendered real content), a later boot reported
`config: source=compile-time ssid=(empty)` and raised the setup portal again. Every key
logged `nvs_get_blob len fail: ssid NOT_FOUND`, yet no `nvs_open failed` appeared, so the
namespace existed but was empty.

**Diagnosis (from an offline dump of the NVS region, preserved before the device was
reset).** The NVS is healthy and correctly formatted — namespace `chromawotd` at
nsIndex=4 with all 16 entries well formed (`ssid`/`pass`/`tz` = 11-byte blobs, `host` 10,
`cfgver` 5, `lat`/`lon` 4, `mode`/`cfgok` u8). But the page sequence numbers start at 0 and
increment, which is the signature of a RECENTLY REFORMATTED partition. The mechanism is in
the Arduino core's `initArduino()`:

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_partition_erase_range(partition, 0, partition->size);   // wipes EVERYTHING
    err = nvs_flash_init();
}
```

The NVS partition is only 20 KB and holds WiFi + BLE + DHCP state alongside our config.
When it fills, the core **erases and reformats the entire partition**, destroying the
stored credentials, and the device falls back to the unprovisioned path. Nothing in our
code erased them; `cc_configEraseNvs()` is only reachable from the deliberate 10 s
factory-reset gesture.

**What is PROVEN vs INDICATED.** Proven: the partition was reformatted recently (page seq
numbers), our entries are well formed, and no code path of ours erased them. Indicated but
not proven: that `ESP_ERR_NVS_NO_FREE_PAGES` was the specific trigger — the boot log did
not contain the core's `Failed to initialize NVS!` line (which only logs if the RE-INIT
fails, so a successful reformat is silent), and the evidence partition was already
rewritten by the re-run portal before it could be captured in the failing state.

**Consequences / what to do.**
- A device can silently lose its configuration and there is nothing in the UI to say why;
  the user sees the setup portal again and may not realise their settings were wiped.
- Do not trust a single NVS write as permanent. A read-back verification after save (and a
  boot-time warning when a previously-provisioned device finds nothing) would surface this
  instead of hiding it.
- If the trigger is confirmed, the fix is to stop sharing one small partition with the
  WiFi stack (a dedicated NVS partition for our config), not to write more defensively into
  the same one.

**Method note (the part I got wrong twice).** I first claimed the page header was corrupt
using `0xFEEDBEEF` as the NVS page magic — that is the DEBUG-STUB magic; I had
misremembered it. I then "confirmed" the bug from that false premise. Only parsing the
entries against the authoritative layout from Espressif's `nvs_types.hpp` produced a
correct reading, which is how the real story (a healthy but freshly-reformatted partition)
emerged. Fetch the struct definition before interpreting binary layouts.

(2026-09-13)


## 46. Reviewing a review: what the Phases 2–5 pass got right, wrong and missed (RESOLVED)

`docs/CODE_REVIEW.md` listed 24 findings against Phases 2–5. Every one was re-checked
against the on-disk source before acting. Two of the lessons here are about how the
findings were FRAMED, not whether they were true.

**All 24 were real, and the citations were accurate to the line.** Verified individually:
the portal-timeout fall-through (`main.cpp:382` logged "sleeping" and then fell straight
into the sync path), a `contentMode` that was stored, validated and offered in the portal
form but never read (`main.cpp:184`), a zero-initialised `g_candidate` that rendered
`(0,0)` — the exact combination validation rejects — an e2e tool whose default timezone
(`AEST-10AEDT,M10.1.0,M4.1.0/3`) is neither an IANA name nor one of the legacy forms
`tz_map.cpp` maps, unsigned `putBytes() >= 0` checks that can never fail, two pinned
Let's Encrypt intermediates, and six inconsistencies including a stale `GPIO2/3/5` comment
in a file that uses `GPIO2/3/8`.

**A "resolved" status is not permanent — re-read the claims made about the fix.**
`docs/STATUS.md` still described Phase 1 and carried a "Next steps" list (button toggle,
NTP sync, Open-Meteo fetch) whose every item was already complete, plus a flatly false line
("`main.cpp` still ships a hardcoded fixture and never sleeps"). A status document that is
not updated in the same commit as the change becomes a source of wrong facts within days,
which is worse than having no document.

**Aggregate findings that share a root cause, and cost them as one piece of work.**
BUG-05 (`>= 0` on an unsigned return) and DES-03 (an empty value does not delete its key)
are the same three lines and the same fix — a `putStr()` helper that compares the returned
length AND removes the key when the new value is empty. Filed separately they read as two
jobs; they are one. Note the subtlety BOTH directions: the `> 0` checks on the other keys
were also wrong, because a legitimately empty value (an open network's blank passphrase)
returns 0 and would have been reported as a write failure.

**A severity label must survive the project's own definition of it.** BUG-01 was filed P0,
where the review's own scale says P0 = "must be fixed before shipping network code". That
code had already shipped, and the defect is a self-recovering UX regression (a button press
re-opens the portal). It is a P1. Inflation at the top of the list devalues the whole list.

**Pin ROOTS, never the intermediates a server happens to be serving.** `net_impl_esp32.cpp`
pinned the Let's Encrypt intermediates YR2 (Open-Meteo) and YE1 (Wordsmith) because those
are what the live TLS chains contained. Both are re-issued on a rotation schedule and both
expire 2028-09-02; the day a different intermediate appears, every fetch fails TLS
verification — and with no OTA that means a physical reflash. Replaced with a bundle of
true self-signed trust anchors: ISRG Root X1, ISRG Root X2 and Amazon Root CA 1, valid to
2035 / 2040 / 2038.

*Verify this class of change on the host before touching firmware.* `openssl s_client
-showcerts` yields the real chain, and `openssl verify -CAfile <bundle> -untrusted <chain>
<leaf>` proves the bundle is sufficient. Doing exactly that caught a second, unreported
defect: the Amazon certificate in the source was the CROSS-SIGNED variant (issuer =
Starfield G2, a certificate the server never sends), which strict verification rejects with
"unable to get issuer certificate". mbedTLS had been tolerating it by matching the public
key, so it worked on hardware while being wrong. The self-signed Amazon Root CA 1 verifies.
Checked 2026-09-18 against all three live chains; **not** exercised on hardware (no port
attached this session).

**An unset clock was being reported as an API outage.** `WiFiClientSecure` validates
`notBefore`/`notAfter` against the system clock, so after a failed NTP sync the clock reads
1970, every handshake is rejected, and three healthy APIs look broken. Now logged
explicitly where it happens and surfaced on the panel as
`PARTIAL: <api> failed (device clock not set)`.

**Fixed here vs deliberately left.** Fixed: BUG-01…BUG-05, DES-01…DES-03, INC-01…INC-06,
DOC-01…DOC-03, plus two extra defects found while verifying — the second stale pin comment
in `main.cpp`'s file header, and ARCHITECTURE.md §4 still describing fallback content and
alert strings the code had already deleted (drift the review only partly cited). Left open:
**BUG-06**, the shared 20 KB NVS partition. Its remediation is a custom `partitions.csv`,
which moves flash offsets — so it needs a full flash, hardware verification, and updates to
the app-only-flash workflow and `merge_firmware.py`. Shipping an unverified partition table
that could brick the boot is worse than keeping a diagnosed bug in the backlog.

The regression is now covered by a host test: `cc_resolveContentMode()` is pure and
`test_sched.cpp` locks the forced-mode behaviour — which is how "a stored setting nothing
reads" should have been caught in the first place.

**A review finding can be right about the smell and wrong about the fix — check it against the
tool's contract.** INC-06 read "`bench_watch.py` hardcodes `DEFAULT_PORT = "COM13"`; it should
fall back to auto-detecting serial ports via `list_serials()`". Reasonable on its face, and I
implemented it literally: resolve the port at startup, and error out if nothing is detected.
That silently BROKE both modes, because the port genuinely does not exist while the device
deep-sleeps — which is the state the tool exists to observe. `--presence` treats absence as the
sleep signal and `--capture` waits for the port to *appear*, so demanding a port up front
guarantees failure in the normal case. It was caught only when actually used, on a sleeping
device, and had been committed as "FIXED".

The correct shape keeps both intents: auto-detect when a port IS present (so the COM number can
change with the USB port/hub), and otherwise fall back to a named port and let the mode WAIT for
it (`FALLBACK_PORT`, with a message saying absence is expected). The distinction that matters is
between a *default* and a *requirement*.

Two general rules fall out:
- **Verify a finding against the artefact's purpose, not just its code.** "Hardcoded value" is a
  smell; whether it is a *defect* depends on whether the value must be present at that moment.
- **A tool that observes a sleeping device must never require the device to be awake.** For any
  wait-for-X mode, the absent state is the entry state, not an error.

(2026-09-19)

## 47. A review fix can pass every test and still break the tool (RESOLVED)

See the closing note of §46: implementing INC-06 as written ("error when no port is detected")
made `bench_watch.py` exit 2 with `no serial port detected` whenever the device was asleep. The
host suite does not cover `tools/` scripts, so nothing failed; the breakage surfaced the first
time the tool was run for real, against a sleeping board, during the flash of the DES-01 build.
Fixed by falling back to a named port and waiting, and re-verified in the failing state (no port
present → the tool waits instead of erroring, exit 0).

The lesson generalises past this repo: **tooling changes are untested code unless you run the
tool in the state it was designed for.** For a bench tool that state is "device asleep with no
serial port", which is precisely the state a developer is least likely to have when they
edit it.

(2026-09-19)


## 48. BUG-06 fixed: our config gets its own NVS partition (FIX IMPLEMENTED, bench run pending)

§45 diagnosed it; this is the fix. The trigger lives in the Arduino core:

```c
// cores/esp32/esp32-hal-misc.c (initArduino)
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);  // <-- NULL label
    esp_partition_erase_range(partition, 0, partition->size);            // wipes EVERYTHING
}
```

We cannot stop the core doing that. We can stop it reaching US: the configuration now lives in
its own `nvs_cfg` partition (`firmware/partitions.csv`), opened with
`Preferences::begin("chromawotd", ro, "nvs_cfg")`. `Preferences::begin()` calls
`nvs_flash_init_partition(label)` itself — verified in the core source — so no extra init call
is needed; the label alone was the whole code change (three call sites).

**The ordering constraint is the load-bearing part, and it is invisible.** The core finds its
erase target with a **NULL label**, i.e. `esp_partition_find_first(type, subtype, NULL)` — the
FIRST matching entry in the table, not the one named "nvs". So `nvs` must remain the first
`data, nvs` row: move `nvs_cfg` above it and the core would erase **our** partition instead, and
the bug would come back pointing the other way. Any future table edit has to preserve this.

**Baseline trap: the docs said `default.csv`; the board builds `default_8MB.csv`.** The plan
told the next developer to copy the framework's `tools/partitions/default.csv` as the baseline.
That table has a `0x140000` app0 and different `app1`/`spiffs` offsets — laying the flash out
wrongly. The board definition (`espressif32/boards/seeed_xiao_esp32s3.json`) actually sets
`"partitions": "default_8MB.csv"`, which is provable from the build itself: its app0 of
`0x330000` is exactly the 3342336-byte app slot PlatformIO prints in every build summary. When
a doc names a file, confirm the artefact really consumes it before copying from it.

**Keeping the offsets identical is what made this safe.** `nvs`, `otadata` and `app0` keep
their baseline offsets, so `tools/merge_firmware.py`'s LAYOUT still holds, an app-only flash at
`0x10000` still lands on app0, and the bootloader at `0x0` is untouched — the update is
`partitions.bin` @ `0x8000` + `firmware.bin` @ `0x10000`, not a full 0x0 merged image. Space
came from `spiffs` (unused, no filesystem code) and `app1` (the OTA slot, unused: there is no
OTA).

**Making the proof deterministic instead of waiting for it.** The original re-test was "fill the
WiFi NVS over many connect/disconnect cycles" — slow, and not guaranteed. Instead two envs do
it head-on: `env:nvsprobe_legacy` (old table) and `env:nvsprobe` (new table) each write distinct
keys into the SHARED `nvs` namespace until writes fail, `ESP.restart()`, and then report on the
next boot whether the shared partition was reformatted and whether our config survived. Running
it as a pair *demonstrates* the fix — one env shows the config destroyed, the other shows it
intact — rather than asserting it. A local check confirms `nvsprobe_legacy` builds a partition
table **byte-identical** to the one previously flashed (md5 `801ba716…`), so the "before" case
really is the old layout.

**A verification script can report a confident falsehood in either direction.** A throwaway
parser I wrote to decode `partitions.bin` declared the new table broken: it claimed no
`data, nvs` entry existed and that the core would therefore erase nothing. The parser had the
wrong constant — `ESP_PARTITION_SUBTYPE_DATA_NVS` is `0x02`, not `0x01` (`0x01` is
`DATA_PHY`). Printing the RAW subtype bytes alongside the verdict is what exposed it. This is
§46's "a green signal that cannot fail" inverted: a RED signal that cannot be right is just as
useless, and the same fix applies — check the checker against values you can see, before
believing its conclusion about values you cannot.

(2026-09-19)


## 49. RTC_DATA_ATTR does not survive ESP.restart() — use RTC_NOINIT_ATTR (RESOLVED)

The NVS fill probe kept its stage flag in `RTC_DATA_ATTR`, set it to 1, called `ESP.restart()`,
and expected to read stage 1 on the next boot. Instead every boot printed `stage=0`, so the
device filled the shared NVS partition and rebooted again — endlessly. From the bench that just
looks like "the device stopped working".

**Why.** `RTC_DATA_ATTR` places the variable in `.rtc.data`, which the bootloader RE-INITIALISES
from the flash image on every reset EXCEPT a deep-sleep wake. That single exception is exactly
what makes it the right choice for state across deep sleep (e.g. the `g_ext1Streak` wake-storm
counter, which is correct as-is). A software reset is not that exception, so the flag was
restored to its initialiser every time.

**Fix.** `RTC_NOINIT_ATTR` puts it in `.rtc_noinit`, which is not initialised across resets at
all. Because a cold boot therefore leaves it holding arbitrary values, it must be validated —
here a magic plus a range check (`stage > 1 -> 0`), so garbage cannot be mistaken for a valid
stage.

Rules:
- **State that must survive `ESP.restart()`/any reset: `RTC_NOINIT_ATTR` + a validity check.
  `RTC_DATA_ATTR` is for deep-sleep wakes only.** Both macros are defined in the core's
  `esp_attr.h` (`.rtc.data` vs `.rtc_noinit`) — read them rather than assuming.
- **A self-rebooting diagnostic needs a guard that itself survives the reboot**, or "run the
  test once" silently becomes "run it forever".
- **The blast radius of the loop was flash wear**: it erased and refilled a partition every ~3 s
  while looping. An endless loop is damage, not just noise.

**The probe also gained a second, independent proof.** Because the behavioural fill may or may
not force a reformat, the probe now FIRST asks the partition table the question directly: which
partition would the core erase (`esp_partition_find_first(DATA, NVS, NULL)`), and which holds
our config (`esp_partition_find_first(DATA, NVS, "nvs_cfg")`)? If they differ, the erase cannot
reach us — a structural answer that is valid whether or not the fill succeeds. Pairing a
deterministic structural check with a best-effort behavioural one is how you avoid a
diagnostic that can only ever say "inconclusive".

(2026-09-19)


## 50. BUG-06 proven on hardware — and the two wrong theories it cost (RESOLVED)

The fill probe settled it. From the device, on the fixed table:

```
probe: core would erase : label=nvs      offset=0x009000 size=20480 B
probe: our config lives : label=nvs_cfg  offset=0x670000 size=16384 B
probe: STRUCTURAL VERDICT = SEPARATED - the core's erase target is a different partition
probe: fill markers still present = no
probe: OUR CONFIG still present    = YES
probe: BEHAVIOURAL VERDICT = shared partition REFORMATTED and our config SURVIVED
```

Two independent checks agree, and they are deliberately different in kind: the STRUCTURAL one
compares the partition the core would erase against the one holding our config (valid whether
or not the fill succeeds), the BEHAVIOURAL one fills the shared partition until writes fail and
reboots so the core's own `nvs_flash_init()` path runs. A diagnostic whose only evidence is the
fragile half can only ever report "inconclusive".

**Then Wi-Fi would not connect — and I got the cause wrong twice.**

*Theory 1: the ~20 reformats had destroyed the PHY calibration in the `phy` namespace, so the
RF chain was deaf.* Plausible, and it justified erasing the shared `nvs` region to force a
recalibration. It was **wrong**. The scan that should have come FIRST came later:

```
diag: scan found 21 AP(s); our configured SSID visible = NO
```

The radio was perfectly healthy. The device was hunting a network it could not see.

**The actual cause: the stored SSID was not the network's name.** After the partition-table
change the config partition is empty, so the setup portal rendered with a BLANK SSID box and
the name had to be retyped by hand — and SSIDs are case-sensitive. A factory reset plus a
careful re-provision fixed it immediately.

Rules:
- **Reach for the direct observation before the inferential repair.** A Wi-Fi scan answers
  "can the radio see anything at all?" in five seconds and costs nothing. Erasing calibration
  to test a theory *about* calibration destroys state to learn what a scan would have said.
- **A change that empties a stored field turns a re-entry into a fresh chance to mistype it.**
  Moving the config partition left the portal's SSID box with nothing to pre-fill. A network
  picker (scan the APs, offer them) would remove that entire class of error — free-text SSIDs
  are a trap on a device whose only local display is a slow e-paper panel.
- **Report the actual failure reason, always.** `wifi FAILED` cannot distinguish "AP not in
  range" from "wrong password" from "join timed out"; a whole bench session went to the wrong
  branch before the firmware was made to print `wl_status_t`. Enumerate the enum from the
  installed header rather than assuming its members — this core has no `WL_WRONG_PASSWORD`.

**And the retry loop that lied.** A background flasher spun for 33 minutes (1800 attempts),
printing "the port may have vanished mid-transfer" every time — while the real error was:

```
esptool write_flash: error: [Errno 2] No such file or directory: '.../.pio/build/s3/bootloader.bin'
```

`.pio/build/s3/` had been removed, so every attempt was doomed before it began and the loop's
own hardcoded message blamed the hardware. `tools/flash_when_awake.py` now (a) verifies the
build artifacts exist BEFORE waiting for anything, failing immediately with the missing paths
and the build command, and (b) prints the tool's real error tail on each failure. Both paths
are covered by a test: a missing env exits 3 instantly, a present env with a sleeping device
waits and then exits 2.

A retry loop may be patient; it must never be credulous.

(2026-09-19)


## 51. Removing the mistyped-SSID class outright, and verifying a save (RESOLVED)

§50's failure had a fixable root: a setup form that asks for a **case-sensitive name typed by
hand**, on a phone, with a slow e-paper panel as the only display — after a config change has
emptied the field so there is nothing to pre-fill. Doing the same thing more carefully is not a
fix; removing the typing is.

**The portal now scans and offers the networks it can see** (`cc_portalScanNetworks`), strongest
signal first, duplicates removed (one SSID spans several BSSIDs), the stored name pre-selected
when present, plus a manual field for hidden networks that WINS when filled so a typed name is
never overridden by a stale selection. The scan happens BEFORE the SoftAP comes up: scanning
needs the station interface, and doing it first avoids AP+STA coexistence (the AP would
otherwise have to follow the scanner onto a channel).

Two rules this leaned on:
- **Put the risky part where a test can reach it.** The option rendering is a pure function
  (`cc_portalRenderSsidOptions`) compiled on the host, not device-only code. That matters because
  an SSID may legally contain quotes and angle brackets and is emitted inside `value='...'` —
  so escaping is load-bearing, and it is now asserted rather than hoped for.
- **Run the negative control on the test itself.** With escaping disabled the escaping test
  FAILS (verified), so it is a real test rather than one that passes for free. A test never seen
  to fail proves nothing (cf. §46, in both directions).

**And the save is verified by reading it back.** `cc_configSaveToNvs()` reloads the whole
configuration and compares every field before reporting success. Return codes could not express
this: the per-key checks had already been found meaningless (`>= 0` on an unsigned return), so
the only honest verification is to ask flash what it actually holds — the same conclusion §45
reached about not trusting a write.

**The loss report has a stated limit, not a hidden one.** An `RTC_DATA_ATTR` "was provisioned"
flag survives deep sleep, so a wipe across a *sleep* now repaints the setup screen under
"Settings lost:" instead of masquerading as a first boot. It does not survive a power cycle,
which legitimately looks like a first boot. The deliberate 10 s factory reset clears the flag,
so a by-design wipe is not misreported as a loss. An RTC-marked "was provisioned" is a good
signal for state that survives sleep and an honest non-signal for anything else — say which.

(2026-09-19)


## 52. A green verify_all.py is NOT a green CI run — reproduce the lint gate locally (RESOLVED)

Pushing this session's work failed CI **twice**, on the `lint` job alone — `build-and-test` and
the secret scan were green both times:

    firmware/src/main.cpp:174:49: error: code should be clang-formatted [-Wclang-format-violations]   (x15)
    tools/bench_watch.py:172:101: E501 line too long (102 > 100 characters)
    tools/flash_when_awake.py:91:101: E501 line too long (102 > 100 characters)

`verify_all.py` covers build + tests + render ledger + alignment. It does **not** run
clang-format, black or flake8 — so "ALL GREEN" locally said nothing at all about two of the three
CI jobs. Worse, the failure CASCADED: the C++ step aborts the job, so black/flake8 never ran and
their two violations stayed hidden. Fixing only what CI reported once would have failed again.

Reproduce the gate exactly, then fix everything in one pass:

    pip install "clang-format==23.1.1" black flake8     # the PINNED version; a different one
                                                        # flags code that is already formatted
    FILES=$(git ls-files ':(glob)firmware/src/*.cpp' ':(glob)firmware/src/*.h')
    clang-format --dry-run --Werror -style=file $FILES
    black --check --line-length 100 tools/
    flake8 tools/ --max-line-length 100

Details worth keeping:
- Use `git ls-files ':(glob)...'`, never a shell glob. An untracked local header can match a
  glob, and clang-format then PRINTS its offending lines — which is exactly how real credentials
  were once echoed into a log. `:(glob)` also stops `*` recursing into `fonts/` and reformatting
  the machine-generated tables.
- The C++ gate covers only TOP-LEVEL `firmware/src`. Changes under `net/`, `config/`, `text/`
  and `draw/` are not checked at all, so formatting there is invisible to CI.
- Run these BEFORE pushing. A red gate on the remote costs a round trip and leaves failures in
  the history; the local run costs seconds.

(2026-09-19)


## 53. The verse gets the whole panel — a design change made for readability (RESOLVED)

This is a verse/word display; the weather column occupied 70 of the panel's 296 px (24%). The
owner reads this panel at 55 and said the text needed to be larger. Measured before changing
anything, through the device font path:

| verse block width | what the ladder picked |
|---|---|
| 218 px (weather column present) | 6pt — already the ladder's ceiling |
| 288 px (weather column removed) | 8pt |

So the weather WAS costing type size, but not where either of us first assumed: at 218 px the
ladder was already at its top rung, so the win came from **raising the ceiling**, and the extra
width is what lets a bigger face still fit its line budget. Final behaviour: 8pt for short and
medium content, 7pt for longer, 5.5pt for the longest — a floor still above the 5pt the old
block gave the worst case.

Approved design:
- **Header band = WHAT you are reading**: the citation, or the word with its respelling, at one
  size up (7pt), plus the date. The mode name ("Verse of the Day") is gone — it was the least
  informative text on the panel.
- **Body = the verse** at the full panel width, auto-sized.
- **Footer row = status/warnings bottom-LEFT** (red) and **weather as TEXT bottom-RIGHT**. The
  icon is gone: the condition words carry more than a 10px glyph that only distinguished four
  states. `TOMORROW` rides with the temperature, in red, only in the evening — without it the
  number is indistinguishable from today's.

**Three defects found while implementing it:**
1. **An unbounded draw.** The warning was drawn with a plain `drawString`, so a long one ran off
   the panel edge. Now `cc_fitText()` truncates with a visible `...` — the same
   never-silently-clip rule the verse block already followed.
2. **A guard with the wrong arithmetic.** The header-vs-date collision guard reserved 8px and
   delivered 2px, because the title's own left margin comes out of the same budget. Fixed to
   `-12`; the *guaranteed* floor is now what it claims.
3. **A test that never tested what it said.** `Unicode.NonBreakingSpaceDoesNotBreakLayout` used
   `\u00C2\u00A0` — a C++ escape for U+00C2 *followed by* U+00A0, i.e. "A-hat" plus a NBSP, not
   the raw NBSP bytes — and its assertion only checked a divider column. Rewritten to assert the
   render is byte-identical to the same text with plain spaces, using `\xC2\xA0`.

**Two measurement traps, both already documented and both re-hit here:** a standalone probe
reported 6pt where the engine said 5.5pt, because without the app's initialisation the wrap
measurement silently collapses (LESSONS §38); and the first attempt measured `layout_render` in
the DEFAULT build, where `cc_verseFontSize()` is a stub that ignores its arguments and always
returns `Pt55`. **Measure the device font path (`build-device`) through the engine's own
reporter.**

Test/tooling updates: `test_verse_autosize` (new rungs + a floor assertion), `test_layout_landscape`,
`test_layout_alert` (footer row), seven assertions in `test_layout_overflow` (the icon tests now
exercise `drawWeatherIcon` directly, since driving them through the layout would have become
vacuous), and `tools/measure_layout.py` — whose invariants are now band extent, body margin,
**footer baseline** (warning and weather must share an ink row) and the right edge.

(2026-09-19)


## 54. A render is only evidence if its inputs are produced the same way the device produces them (RESOLVED)

The header date read **`2026-09-19` on the device** while every render — the ones the design was
approved from — showed **`Fri, Sep 12`**. Not a formatting bug but a *provenance* bug: the device
built the string with `snprintf("%04d-%02d-%02d")` in `main.cpp`, while the harness was handed a
hand-written literal (`--date "Fri, Sep 12"`, and the test fixtures hard-coded the same text).
Two producers, one field, no shared code — so the renders could look right while the panel was
wrong, and **no test could ever have caught it**. The approval was based on an artifact that
could not have been evidence for that field.

Fix: one pure, host-tested `cc_formatHeaderDate()` (`text/date_format.{h,cpp}`) producing
`DOW DD MMM` (`Sat 19 Sep`), called by **all four** consumers — the device (from the NTP
`struct tm`), the layout tests (which generate the ledger), `layout_render` via `--date-ymd`, and
therefore `render_preview` / the fixture (now `date_ymd`).

Rules:
- **A render proves the LAYOUT and nothing about the VALUES it is handed.** If a field is
  formatted anywhere, the render path must obtain it through the same code as the device, or the
  render is silent about it. Ask, for each field in an approved render: *where does this string
  come from on the shipped device, and is it the same producer?*
- **Format once, in a shared, tested function**; make every consumer call it. The tests now build
  their date with the very function the panel uses, so the two cannot drift.
- **Prefer explicit tables to `strftime("%a %b")`** here: those conversions are locale-dependent
  and this device never sets a locale, so an identical build could render differently.
- Tests pin the contract: the exact `DOW DD MMM` form, zero-padded day, every weekday/month
  token, out-of-range input degrading to `---`, and a regression guard that the output contains
  no `-` or `,` (i.e. never the ISO form the panel used to show).

Same change: the date is drawn at the **same size as the identity line** (7pt), so the band reads
as one line instead of a large left item beside a small right one — and the collision guard
measures with that same font, so its reserve is honest rather than optimistic.

(2026-09-19)


## 55. Raising the ladder's ceiling to 10pt, for short content (RESOLVED)

With the verse owning the panel (§53), the remaining waste was visible: a Word-of-the-Day
definition is typically short, and at the 8pt ceiling it sat in the top half of the block with
the lower third empty. The ladder is now **10 / 9 / 8 / 7 / 6 / 5.5 / 5pt** (Roboto 9 and 10pt
tables generated from the same source TTF as the rest).

Measured on the device font path, the ladder's picks are now:

| content | before | now |
|---|---|---|
| Word-of-the-Day definition (~75 chars) | 8pt | **10pt** |
| very short verse (11–56 chars) | 8pt | **10pt** |
| the 153-char fixture verse | 8pt | 8pt (unchanged) |
| 191 chars | 7pt | 7pt |
| 316 chars (longest realistic) | 5.5pt | 5.5pt |

So the change is additive: short content gains two rungs and long content is untouched — which is
the point of a ladder rather than a font choice. Cost: ~3.8 KB of flash for the two tables.

*Not* addressed, and now the visible consequence: a two-line verse can leave the lower third of
the block empty, because the block is TOP-aligned and the ladder cannot fix that — a bigger font
would need a rung the height budget cannot hold. The honest next step, if it bothers the eye, is
vertical centring of the verse block (a layout change, not a font change). Flagged rather than
quietly added.

Tests: the two "largest rung" expectations moved to `Pt10`, and a case was added for the actual
motivating content (a word definition). The ladder's monotonicity test still passes unchanged,
which is the property that keeps a longer text from ever selecting a bigger face. The render
ledger is unaffected: it is built from the default host build, where the ladder is compiled out
(LESSONS §38), so those renders cannot move with a font decision.

(2026-09-19)


## 58. A double click is NOT detectable on this board — measured, then a short hold instead (RESOLVED)

The owner asked for a **double-click** gesture to switch between Word and Verse. Rather than reason
about it, a probe (`env:gestureprobe`) measured the real observation window on hardware.

**What it measured.** Sampling the RTC pads as the FIRST statement in `setup()` — before the 2 s
serial delay — the firmware can observe them **84,493 / 84,494 us after the app starts**
(repeatable to 1 us across four wakes). A TAP then releases **106 / 116 / 120 ms** after that first
sample. Press durations were 97-163 ms; double-click gaps 104-126 ms.

**Why the double click fails.** Across two batches, EVERY button wake showed the pad **still LOW
at the first sample** — four of four single taps and three of three double clicks. The mechanism:
the press IS the wake source, the ROM bootloader runs before any of our code, and by the time the
firmware can look, the first click of a double click is already over. What we observe is the tail
of whichever press is in flight, and a single tap looks identical. So "pad low at first look" —
the obvious discriminator — is not one, and it is also how the factory-reset hold begins. The rule
I had proposed would have fired a toggle on ordinary single taps.

**What was built instead: classification by HOLD LENGTH** (`sched/hold_gesture.*`, superseding
`factory_reset.*`). A tap releases ~120 ms after our first sample while a hold keeps going, so
duration separates them with a wide margin:

    tap                 -> sync + refresh (unchanged)
    hold 0.5 s - 10 s   -> invert the content mode for this refresh
    hold >= 10 s        -> factory reset (unchanged)

Details worth keeping:
- **The sampling must run BEFORE the serial delay.** The shipping build sampled after it, so even a
  1 s hold was over before it was seen. Moving only the SAMPLING preserves the delay's purpose
  (USB enumeration before the first log lines) — and the boot log is a first-class test artifact
  for this project, so it is not a cost worth paying twice.
- **The override is transient by design.** An `RTC_DATA_ATTR` invert flag survives deep sleep, and a
  SCHEDULED (timer) wake clears it, so a hold can never leave the device stuck on the wrong half of
  the day: the clock is in charge again at the next slot.
- **The threshold carries a measured margin, not a guessed one.** 500 ms is ~4x the observed tap
  release; the reset threshold stays at PROJECT_PLAN's 10 s.
- **A gesture that is indistinguishable must be reported as such.** The probe's own verdict line
  read "a double click is NOT observable at this boot latency", and that is what closed the
  question. It is cheaper to build a probe than to ship a gesture that misfires.
- **Superseding is better than duplicating.** The new classifier REPLACED the reset-only module
  rather than becoming a second sampler, so there is one place that decides what a button press
  means (and the reset's 10 s accounting is still counted from the wake, as before).

Tests: seven for the classifier (tap; short hold while still held; a release after a hold NOT
re-reporting as a tap; escalation to reset; never firing twice; a single contact sample still a
tap; progress) plus `cc_invertContentMode`. Verified they can fail — collapsing the short-hold
threshold to 0 fails three of them.

(2026-09-19)


## 59. A green test for a pure function does not test the code that CONSUMES it (RESOLVED)

The short-hold gesture shipped and **did not work**: a real capture showed `gesture: tap` for a
tap but *nothing at all* for a 1 s hold. The classifier was correct — 7 tests, all green — and the
defect was in the fifteen-line loop in `main.cpp` that drove it:

```cpp
if (g == HoldGesture::ShortHold)
    continue;                    // keep watching for a Reset... and DISCARD the ShortHold
...
if (st.decided)
    return HoldGesture::None;    // <-- the pending ShortHold was thrown away here
```

`cc_holdFeed()` deliberately reports ShortHold **while the pad is still down**, so that a hold
which keeps going can still escalate to a Reset. The loop kept watching (correct) and then
discarded the latched result (wrong). The API required every caller to remember a pending value,
and its only caller was device-only code with no test.

Rules:
- **A unit test proves the function, not the integration.** When a pure function hands back a value
  that the caller must hold and act on later, that "remember it" step is logic, and it needs its own
  test. Ask of any new API: *what must the CALLER do correctly for this to work — and is that
  covered anywhere?*
- **Move the caller's logic into the tested module.** The fix was not to patch the loop but to
  relocate it: `HoldSession` (begin/feed) now lives in `hold_gesture.cpp` with 4 tests, and
  `main.cpp` merely ticks it. Device-only loops are untestable by construction, so decisions do not
  belong in them.
- **This failure was invisible to every host test and to the build.** The suite was green
  throughout; the only evidence was a bench capture where one gesture printed a line and the other
  printed none — the same shape as §44's "the boot log is a first-class test artifact". A gesture
  is hardware behaviour; it has to be run on hardware.
- Verified the new test catches it: re-introducing the exact `return HoldGesture::None` makes
  `HoldSession.AShortHoldIsReportedWhenThePadIsReleased` fail, and reverting restores 4/4.

(2026-09-19)


## 60. Two processes on one serial port: the flash loses the race, and it reads as a device fault

Twice in one session a flash appeared to fail against a perfectly healthy bench. The cause was never
the device: **a capture process was still running and polling the same COM port**, so it won the race
to open it every time the device woke and the flasher was told "the port is busy or doesn't exist".
One episode took **18 attempts** before landing; the flash finally succeeded only when the operator
pressed a button, which had nothing to do with it.

**The tell is the error, not the attempt count:**

| message | meaning |
|---|---|
| port ABSENT / `pio device list` empty | the device is deep-asleep — expected, wait or press a button |
| **"port is busy" / `PermissionError(13, 'Access is denied')`** | **someone else holds it** — find and kill the other process |

Rules:
- **Stop any capture before arming a flash.** The port is a single exclusive resource on this bench:
  the flasher, `bench_watch`, `flash_when_awake`, a follow-capture and `verify_flash` all need it, and a
  long-running observer will starve every one of them.
- **Read the error, not the retry count.** "Absent" and "busy" have opposite remedies.
- **`flash_when_awake.py` hides this by design.** It retries until the deadline, so the symptom is a
  long silent wait rather than a failure — a high attempt count in its output is the signal to go
  looking for a competing process, not to keep pressing the button.

(2026-09-19)


## 56. Morning wake moved to 06:00 — and why the old numbers stay in the history (RESOLVED)

Requested change: the three slots are now **06:00 / 12:30 / 18:00** (`sched/wake_schedule.cpp`).
The schedule is a pure function whose boundaries are pinned by tests, so they moved with it: the
slot table, midnight→morning (6 h, was 6.5), "just before the slot" (05:59 → 60 s), "exactly on
the slot" (6.5 h to midday, was 6), and the evening rollover (18:00 → 06:00 = exactly 12 h).
17/17 pass. The product change is **one line**; everything else is tests and documentation, which
is the payoff of having kept the schedule pure.

**The deliberate omission.** Several lessons entries and code comments cite *"the 06:30 wake was
scheduled for 05:30"* as the **symptom** of the old timezone bug. Those were left untouched. They
record what actually happened, at the slot that existed at the time; rewriting the number to 06:00
would falsify a real defect's evidence in order to match a later preference. The rule: **update
statements about the CURRENT configuration; leave statements about PAST events alone**, or the
history stops being usable to whoever debugs a similar bug next.

- Left as historical: `LESSONS` §27/§44, `config/config.cpp`, `config/tz_map.h`, `main.cpp`'s TZ
  comment, `net/portal.cpp`, `test_config.cpp`.
- Updated as current state: ARCHITECTURE, STATUS, PROJECT_PLAN, README, `platformio.ini`,
  `main.cpp`'s behaviour header, `test_sched.cpp`'s header.

(2026-09-19)


## 57. Two ways a step silently doesn't happen (RESOLVED)

Both cost real time here, and neither announced itself.

**1. A capture that misses the boot lines.** A value the device prints only at BOOT — here
`sync: time OK <date> <time>`, the only on-device evidence of the header date format — stays
unverified if the capture attaches after that line has gone by. A throwaway "reset with RTS/DTR,
then read" script attached mid-cycle on several attempts (its first line was already mid-sync), so
the date format was never observed directly and rested on a byte-verified image plus unit tests
instead. The tool that works is `tools/bench_watch.py --capture`, which WAITS for the port to
appear on a natural wake and attaches with DTR/RTS deasserted — that is how the complete
button-wake boot log was captured earlier in this project. **If a value is printed only at boot,
capture the boot: wait for the port to APPEAR — and if you reset instead, verify that the reset
actually happened**, because a silent reset failure looks exactly like a device that is merely
mid-cycle.

**2. A commit the end-of-file hook quietly revoked.** Twice, `git commit` was aborted because the
`end-of-file-fixer` hook rewrote files lacking a trailing newline — and because the hook then
conflicted with pre-commit's own stash/restore, it *rolled its fix back*, leaving the tree clean
and the commit unmade. The log does say the hook failed; the trap is that the "nothing to commit"
which follows reads like an ordinary no-op. **End every file you create with a newline**, and
**read the commit hash back** afterwards instead of assuming it landed.

It happened a **third** time on a markdown document (`docs/UI_HISTORY.md`) written through the
agent's own file-writing tool, which is the giveaway: the rule is not "generated files", it is
**every file you create**. The font tables from `tools/font_convert.py` were fixed at the source
(that tool now emits a trailing newline), but a hand-authored document trips the same hook, and
the failure mode is identical — the hook rewrites the file, pre-commit's stash/restore conflicts
with that rewrite, the fix rolls back, and the commit is simply not made.

(2026-09-19)

## 61. Removing a feature from the DISPLAY is not removing it from the codebase (RESOLVED)

The owner asked, of a doc that still described weather icons: *"those don't exist anymore"*. They
were right about the display and wrong about the code — which is the interesting part. When the
verse-first redesign removed the icon from the panel, it left behind:

- `WeatherIcon` and `WeatherData::icon`
- the WMO→icon mapping in **both** net paths (`net.cpp` for host, `net_impl_esp32.cpp` for device)
- `draw/weather_icon.{h,cpp}` — a whole drawing module
- the `--icon` flag in `layout_render` and `render_preview`, and the fixture's `icon` field
- `tools/weather_icon_sheet.py`, which rendered a sprite sheet for it
- two host tests that called `drawWeatherIcon` directly (rewritten in §53 precisely so they would
  not go vacuous — they became the only remaining caller)
- and a stale `verse_template.html` browser mockup of the pre-redesign layout

**Nothing in the product drew an icon.** The module survived on test-and-tooling references alone,
which is exactly the "dead code" the project's own review methodology tells you to grep for — and
the tests were keeping it alive rather than covering it.

Rules:
- **A UI change is not done when the pixels change.** Grep the feature's *symbol* and delete what
  remains: the enum, the field, the mapping, the drawing module, the tooling flags, the generator,
  and the tests that only exist for it. "The layout no longer calls it" leaves dead weight with a
  passing test suite attached.
- **A test can keep dead code alive.** Those two tests were legitimate coverage when the icon was
  drawn; after the redesign they were the only caller, so "it is tested" and "it is used" became
  the same false comfort. When a feature is withdrawn, its tests go with it.
- **Docs describe the product, so they drift with it.** Every reference — README struct listings,
  the repo tree, architecture diagrams, example commit messages in CONTRIBUTING, the module map —
  had to be swept. A doc that describes a feature the product no longer has is worse than silent:
  it sends the next reader looking for code that was deleted.
- **It resolved a known limitation for free.** §35 accepted that Drizzle/rain drew the wrong icon.
  With the icon gone and the condition as text, that limitation simply ceased to exist.

(2026-09-19)

## 62. The first release published with NO notes — an unknown action input is a warning, not a failure (RESOLVED)

The Release workflow's first run (`v0.1.0`) reported **success** and attached all three binaries. It
also published a release with an **empty body**: the step passed `body_file:`, and
`softprops/action-gh-release@v2` has no such input — its list is `body`, `body_path`, `files`, ...
GitHub Actions treats an unrecognised input as a *warning*, so the step went green, the release was
created, and the notes (the changelog **and** the flash procedure the generator exists to produce)
never reached the page.

The only traces were an annotation on the run — `! Unexpected input(s) 'body_file', valid inputs are
[...]` — and a release whose body length was 0.

Rules:
- **A green step is not evidence that the effect happened. Read the artifact back.** The workflow now
  ends by fetching the *published release* and failing if the body is short or any asset is missing.
  The release page is the only thing a consumer sees, so that is what gets checked.
- **Read the run's ANNOTATIONS, not just its conclusion.** Warnings are where "it worked, but not
  really" lives; the job summary said success throughout.
- **When an action takes a file, read its input list rather than guessing a plausible name** —
  `body_path` here, not `body_file`.

(2026-09-19)

## 63. A flag's name is not evidence of what it guards (RESOLVED)

`-DCHROMAWOTD_FONT_FREESANS` guards the **Roboto** set. `firmware/src/fonts/` contains only
`Roboto{5,55,6,7,8,9,10}pt7b.h` and `RobotoT10pt7b.h`, and the ladder compiled inside that flag
includes those headers. The FreeSans work that named the flag was superseded on 2026-09-12
(8pt crowded the weather column, 6pt fitted but sat wrong on the baseline, mono-hinted Roboto
5.5pt won the comparison).

The name then propagated on its own. `firmware/platformio.ini` pointed at
`firmware/src/fonts/FreeSans6pt7b.h` — **a file that does not exist** — and `target_seeed.cpp`,
`target_canvas.cpp`, `target.h`, `font_types.h` and `test/CMakeLists.txt` all described the
proportional path as "the FreeSans" one, while `verse_display.cpp`'s ladder comment still said
"Roboto 6 / 5.5 / 5pt" long after the ladder became 10/9/8/7/6/5.5/5pt.

Rules:
- **Check the artefact, not the identifier.** Before trusting a name — flag, macro, enum, comment —
  open what it actually loads or calls. Grepping `FreeSans6pt7b.h` finds comments and no file.
- **A renamed thing leaves its old name in three places:** the identifier, the comments that
  explain it, and the docs that cite it. All three were stale here; fixing only one looks like
  progress and leaves the next reader misled.
- When the name is too expensive to change (a build flag referenced by CI, docs and habit), say so
  **at the definition** — the `platformio.ini` comment now states that the name is legacy and names
  the fonts it guards.

(2026-09-19)

## 64. A headline figure in STATUS is a claim about one build (RESOLVED)

`docs/STATUS.md` carried "Flash 9.6%, RAM 5.8%". By the time it was read as current, the
proportional Roboto set and the 10pt-wide auto-size ladder were compiled in, and a clean build
measures **Flash 30.8% (1,030,889 / 3,342,336 B), RAM 23.8% (78,004 / 327,680 B)** — a third of the
quoted flash figure, in the number a reader uses to judge remaining headroom.

The same sweep found the version line still reading 0.1.0 after `v0.1.1` had been tagged and
published, and a BUG-06 row saying "deliberately still open" while the table above it and
`PROJECT_PLAN.md` both recorded it fixed and verified on hardware.

Rules:
- **Re-measure, don't re-quote.** `python tools/verify_all.py --clean` prints both numbers; a figure
  in a status doc should say what was measured and when, so it can be aged.
- **Rows in a status doc must agree with each other.** The BUG-06 contradiction was inside one
  file — the review-fix row said open, the feature row said verified — and survived because each row
  was written on a different day and nothing compared them.
- **Fix every copy.** `docs/ARCHITECTURE.md`'s header carried the same stale version and date, so a
  correction that stopped at STATUS would have left half the drift in place.

(2026-09-19)

## 65. The field that justifies a source is the first thing a degrade rule drops (RESOLVED)

Reported: *"not seeing the pronunciation"* on the Word-of-the-Day panel.

A.Word.A.Day was chosen for exactly ONE property — it publishes a **respelling** and not IPA (§33,
`docs/research/WORD_APIS.md`). The identity line was then assembled as a single string
(`word (res-pell-ing)`) and, when the pair did not fit beside the date, the old rule degraded by
**dropping the respelling altogether** and drawing the headword alone.

Measured at the shipped 7pt identity size (room = 208 px once the date and both margins are
reserved), over the 18 most recent A.Word.A.Day entries:

| Headword | word + respelling @7pt | Result |
| :--- | :--- | :--- |
| `soporiferous (sop-uh-RIF-uhr-uhs)` | 212 px | **dropped** |
| `dataveillance (day-tuh-VAY-luhns)` | 211 px | **dropped** |
| `despotocracy (des-puh-TAH-kruh-see)` | 236 px | **dropped** |
| the other 15 | 105–207 px | shown |

`soporiferous` — the word that was on the panel when this was reported — is one of the three.

Rules:
- **Degrade the cheap field, never the informative one.** The respelling now SHRINKS through
  7 → 6 → 5.5 → 5pt (every one of those fonts is already compiled in for the body ladder, so it
  costs no flash) into the room that is left, and is truncated with a visible `...` only when even
  5pt cannot hold it. The headword keeps its size and is truncated first, as before.
- **A deliberate degrade is still a bug if it fires often.** This one was documented here (§33), in
  the layout comment and in `ARCHITECTURE.md` — which is exactly how it survived. What was never
  measured was how OFTEN it fired: one word in six is not an edge case. Measure the rate before
  calling a fallback acceptable.
- **Test the RELATION, not a pixel.** The test renders the same panel with and without the
  respelling and requires the header band to differ (and the body to be byte-identical). Verified
  to FAIL with the old drop rule restored. Note it only reproduces on the **device font path**
  (7pt): on the 5x7 fallback the pair fits, the old rule never fired, and the test would have
  passed for the wrong reason — the decisive run is `verify_all.py` stage 3.

(2026-09-22)

## 66. A cap applied before the renderer makes the renderer's honest truncation unreachable (RESOLVED)

Reported: *"text is incomplete for the usage of the word, but there is still space for another 1.5
lines of text."*

Two 230-byte caps cut the A.Word.A.Day usage example before the layout ever saw it: `cc_parseAwad`
copied every field into `NET_TEXT_MAX` buffers, and `main.cpp` then assembled definition + example
into a third 230-byte buffer. The example is a whole paragraph — measured **855 chars** for
`misgiving` (2026-09-22), 223 for `abjective` — so the panel received ~272 chars, cut mid-sentence.

The visible consequence is subtler than "text is missing": because the shortened string then FIT
the block, `cc_wrappedLineCount() > cc_lineCapacity()` was **false**, so the overflow marker the
block reserves space for was never drawn. The panel looked like a complete example that simply
stopped, and the free lines were identified as a layout problem rather than a data one.

Rules:
- **The renderer owns truncation, and it can only mark a cut it can see.** Never pre-cut remote text
  with `snprintf`: keep the field whole (`WORD_FIELD_MAX` = 1024, static BSS) and let the block
  decide *and mark*. A cap in front of the marker logic converts a visible cut into an invisible one
  — the worst of both.
- **A preview must compose text exactly as the device does.** `layout_render --word-live` built this
  body from `std::string` with no cap, so every preview looked right while the panel was cut. The
  composition now lives in ONE shared function (`cc_composeWordBody`), and `--word-file <saved.html>`
  renders a specific day's page through it — which is how this was reproduced and fixed.
- Regression tests assert the FIELD survives (`strlen(example) > 230`, body > 230) and were verified
  to fail against the old caps.

(2026-09-22)

## 67. A source's day boundary is not your day boundary (RESOLVED)

Reported: *"seeing the same word of the day as yesterday."*

A.Word.A.Day publishes its next edition at **00:01 US Eastern** — the page stamps itself
(`Sep 22, 2026`, `?date=2026-09-22`; the RSS `pubDate` reads `Tue, 22 Sep 2026 00:01:03 EDT`).
That instant is **14:01 AEST / 15:01 AEDT**, i.e. AFTER the device's 12:30 local slot. So the
afternoon refresh was reading the PREVIOUS edition — the same word the previous evening's 18:00
slot had already shown.

Measured on the bench: the 18:00 AEST refresh on 21 Sep and the 12:30 AEST refresh on 22 Sep
rendered the same word (`soporiferous`), while `misgiving` had been published at 00:01 EDT on the
22nd. Without an edition stamp anywhere on the panel, that is indistinguishable from a device
that never refreshed.

Rules:
- **Read the SOURCE's own edition stamp; never infer freshness from the local clock.** The page
  carries its date twice (a `?date=YYYY-MM-DD` link and a sidebar `Sep 22, 2026`), and
  `cc_parseAwad` returns it as `WordData.editionDate`. No date found → `nullptr` → behave exactly
  as before; never assume "today".
- **"The newest published" and "today's" are different claims, and the panel must pick one.** When
  the edition is not the device's local date, the word slot now renders the VERSE and the word
  appears (fresh) at the next slot past the source's day boundary. Repeating content reads as a
  broken device; there is nothing else it can look like.
- **This project has now lost a refresh to a time assumption twice, in opposite directions**:
  Open-Meteo rejected a POSIX TZ string so every weather fetch failed silently (§44), and a US
  Eastern publish time made the word look stale. Today/zone assumptions belong in tested code or in
  the source's own data — never in a comment.
- **Follow-up (§70):** the window itself was then moved to 15:00 local, and 15:00 turns out to be a
  boundary rather than a guarantee — the table in §70 is the reason the edition-date check stays.

(2026-09-22)

## 70. A window that opens before the source publishes guarantees nothing (RESOLVED)

After §67 the device compared the page's edition date with its own and fell back to the verse when
they differed — correct, but the reader still *saw* a verse at 12:30, an hour that had shown a word
for weeks. The request was to move the Word-of-the-Day window so the word is "definitely already
out": **15:00** local (`kCcWordOfDayStartHour`).

The arithmetic, because "3pm" is a local answer to a foreign clock (the source publishes at
**00:01 US Eastern**):

| Melbourne | US Eastern | A.Word.A.Day flip, local time | 15:00 window opens… |
| :--- | :--- | :--- | :--- |
| AEST (UTC+10) | EDT (UTC-4) | 14:01 | 59 min after ✓ |
| AEDT (UTC+11) | EDT (UTC-4) | 15:01 | 1 min **before** ✗ |
| AEDT (UTC+11) | EST (UTC-5) | 16:01 | 61 min **before** ✗ |

Those are not hypothetical rows: AEDT+EDT happens in October and March, AEDT+EST from November to
March. 15:00 is therefore a *good* boundary, not a guaranteed one — which is exactly why the
edition-date check stays. The two mechanisms divide the work: the window keeps the **scheduled**
refreshes honest (06:00 and 12:30 are verses, 18:00 is the word), and the check covers the boundary
hour and any button tap inside it.

Rules:
- **Convert the source's clock before choosing a local boundary, and write the table down.** "00:01
  ET = 14:01 local" was true for eight months of the year and wrong for four; the single-line version
  of that fact is what made 15:00 feel safe.
- **A clock rule and a data check are not alternatives.** The clock decides *when to look*; the data
  decides *whether what came back is today's*. Keep the data check for correctness and the clock for
  predictability — they cover different failures, and deleting either on the grounds that the other
  exists is how a boundary hour becomes a repeat.
- **Say what a change does NOT cover.** 15:00 removes the scheduled midday repeat; it does not make
  a 15:30 refresh in December safe. The docs state the boundary hour explicitly so nobody reads the
  window's name as a guarantee.

**Revised the same day (shipped in 0.1.2): moved to 16:30.** The table above was written *after*
15:00 had already been chosen, and it says plainly that 15:00 is one minute short in AEDT+EDT months
and 61 minutes short in AEDT+EST months — i.e. the guarantee the owner asked for ("so definitely
already out") did not hold for four months of the year. The window is now **16:30**, the earliest
boundary that clears the worst case with margin (29 minutes).

That revision forced an API change: the policy worked in whole local **hours**, and no hour boundary
can express "after 16:01" except 17:00. `cc_contentModeForMinutes(minutesOfDay)` replaced
`cc_contentModeForHour(hour24)`, and `main.cpp` passes `tm_hour * 60 + tm_min`.

- **When the answer is not a whole hour, the API is wrong, not the answer.** The hour-only signature
  was fine while noon was the boundary and became a constraint the moment the requirement was "29
  minutes after a foreign clock's flip". Widening the unit is a five-line change; rounding the
  requirement to the next hour is a behaviour the owner did not ask for.
- **Write the table before choosing the boundary, not after.** Here the ordering was the other way
  round and it cost a second commit, a second flash and a second set of doc edits — cheap, but
  avoidable, and the same table was already the reason the check existed.

(2026-09-22)

## 68. A box that reserves a fixed pixel inset holds the wrong number of lines (RESOLVED)

Reported *after* §66 was fixed and the whole example finally reached the renderer: *"the misgiving
word should allow for another line, as there is space."*

Measured on that render (5pt rung, 13 px lines, footer row at y=115): six body lines with ink from
y=22 to y=96 and **18 px — a whole line — unused**.

Two causes, both of the same shape:

1. `cc_lineCapacity(maxH, size, lineHeight)` returned `(maxH - 8*size)/lineHeight + 1`: the box
   reserved a fixed **8 pixels** for "the line's footprint", whatever the font. 8 px is 62% of a 5pt
   line and 31% of a 10pt line, so it over-reserved at the small rung and under-reserved at the large
   one.
2. The draw loops stopped on a *different* bound again (`curY + 8 > startY + maxH`) — the same class
   of mismatch that produced §66's invisible cut.

The cheap fix — "just make the box taller" — is a trap: at 10pt a 4th line would finish at y=126,
straight through the footer row at y=115.

Fixes:
- `cc_lineCapacity(maxH, lineHeight) = maxH / lineHeight` — a line fits WHOLE, and the `size`
  parameter is gone. There is no reserve left to get wrong.
- The box is now defined by the two fixed rows rather than a figure of its own: top = `kHeaderH` +
  `kVerseMargin` = 22, bottom = the footer row's top (`kRowY` = 115) → **`kVerseMaxH` 82 → 93**. That
  is 7 lines at 5pt, 6 at 5.5pt, 5 at 6/7pt, 4 at 8/9pt, 3 at 10pt; the deepest ink lands at y=114.
- Both draw loops stop on `lineIdx >= capacity` — the *same* bound the truncation decision uses, so a
  count and a draw can no longer disagree about how many lines exist.
- The captive portal's wrapped error text keeps its 9 lines by moving its box 84 → 90, so the change
  is not allowed to cost a line somewhere the report did not mention.

Rules:
- **A reserve expressed in pixels does not scale with the thing it reserves for.** Derive line counts
  from the line height; a constant standing in for it will be simultaneously too big and too small in
  one layout.
- **Check both ends of the range before resizing a box.** The request was at the smallest rung; the
  risk it added was at the largest. The 93px box is only safe because `93/26 = 3` still holds 10pt.
- **Measure the ink, not the intent.** The real free space was visible in one command — the rendered
  PNG's ink bands (`y` ranges) showed 22–96 with the footer at 115. The layout code and the tests both
  agreed with the *old* rule; only the pixels disagreed.

(2026-09-22)

## 69. A retry loop retries permanent errors too (RESOLVED)

Flashing the device after the WOTD fixes, `tools/flash_when_awake.py` burned a 7-minute window and
then a 15-minute one on the same failure. The port **did** appear (COM13 — the operator's button
presses worked) and the tool "uploaded" 144 times, each attempt ending in:

```
…\venv\Scripts\python.exe: No module named platformio
```

Root cause: the upload command was built as `sys.executable -m platformio`, and `sys.executable` is
whatever interpreter launched the tool — here a venv with no PlatformIO, while PlatformIO Core 6.1.19
lives under `C:\Python314` and is reached through the `pio` on PATH. `tools/verify_all.py` had been
resolving exactly that with `shutil.which("pio")` all along. The retry loop then converted an
environment error into 144 pointless attempts, each one consuming a wake window the operator had to
produce by hand at the device.

Rules:
- **Retry only what a retry can fix.** Classify the failure text and abort on
  "No module named…" / "not recognized as a command". A retry budget belongs to transient faults
  (a port that vanished mid-transfer), never to a missing dependency.
- **Pre-flight the ENVIRONMENT, not just the inputs.** This tool already checked its build artifacts
  before waiting — an earlier lesson — and that check passed while the command it was about to run
  could never work. `pio --version` costs one second and turns a silent 15-minute failure into an
  immediate message.
- **Never hardcode the interpreter for a subprocess that needs a toolchain.** Ask `shutil.which`
  first; keep the module form as a fallback. Two tools in this repo now share that rule instead of
  one of them having it.
- **The evidence was in the log the whole time.** Every attempt printed the real error; the failure
  survived because "uploading…" was taken as evidence. Print the real error *and* read it.

Verified: with PATH stripped of both `pio` and the module, the tool exits 3 with the real message
before it waits for a port; in the real environment it prints `platformio: PlatformIO Core, version
6.1.19` and the flash then succeeded on the **first** attempt.

(2026-09-22)

## 71. A check that only runs on the server is not a check (RESOLVED)

Pushing `v0.1.2` turned CI **red** on the `lint` job while every stage of `verify_all.py` was green.
The offending line was a 103-character `PERMANENT_MARKERS` tuple added to `tools/flash_when_awake.py`
one commit earlier — `black --check --line-length 100` rejected it, `flake8` flagged `E501` on it,
and neither ran locally.

This was not a surprise: §42 recorded it ("`verify_all.py` does NOT run the lint job") and the note
had been carried in the working skill ever since. It still happened, which is the point — a warning
in a document is not a gate.

Fixes:
- `verify_all.py` gained **stage 6**: `black --check --line-length 100 tools/` and
  `flake8 tools/ --max-line-length 100`, run with `sys.executable` exactly as CI runs them. The
  linters are optional locally, so when they are not importable the stage says so and passes — it
  never stays silent, which is what made the gap invisible.
- The linters were run locally in a scratch `uv venv` to fix the file with the SAME versions CI
  installs (`black 26.5.1`, `flake8 7.3.0`) rather than by hand-formatting to a guessed style.

Rules:
- **If CI enforces it, either run it locally or stop calling the local suite "the gate".** The
  cheapest correct option was 30 lines in the existing tool: same two commands, same interpreter.
- **A tool that can only be exercised on the runner cannot be trusted by the person pushing.**
  `verify_all.py` claimed to check everything; now it does, and the claim is testable.
- **Verify the new check fails when it should.** Stage 6 was proved by planting an over-long line in
  `tools/` (it failed and printed `E501`), not by observing that a clean tree passes — a check that
  never fires proves nothing.
- **Carry the fix into the skill the moment it exists.** The stale note ("does NOT run the lint job")
  was updated in the same session; a skill that contradicts the tool is worse than no note.

(2026-09-22)
