# CHROMAWOTD — Lessons Learnt

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
`verse_display.cpp`, which consumes one *glyph* at a time and emits one ASCII byte:

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
[`docs/images/`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/images/).
This tracks light, inverted, dark, alert and overflow-marker variants across portrait
and landscape orientations for future reference.



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
