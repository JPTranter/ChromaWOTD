# CHROMAWOTD — Lessons Learnt

Inherited from the sibling eClock project (read `../eClock/docs/lessons/LESSONS_LEARNT.md`
for the full history). New lessons get appended here with continuing numbers,
starting after eClock's highest section.

## 1. (inherited) Paged loop rule
Draw every pixel of a frame inside `firstPage()/do/while(nextPage())`. Content
drawn before `firstPage()` is wiped from the buffer.

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
2. Exclude subfolders from the library build with
   `lib_build_src_filter = -<Extensions/*> -<Processors/*> -<Touch_Drivers/*> -<TFT_eSPI.cpp>`
   and `#include "TFT_eSPI.cpp"` from main.cpp.
3. Stock PlatformIO has no `seeed_xiao_esp32s3_plus` board; use
   `seeed_xiao_esp32s3` (Seeed GFX drives panel pins by raw GPIO number).
4. Seeed_GFX is not on the PlatformIO registry — depend on the GitHub URL.
   Verified combo for the 2.9" quad-colour (JD79661/JD79667 driver) panel: 512.
   EE05 pin map (from EPaper_Board_Pins_Setups.h): SCLK 7(D8), MOSI 9(D10),
   CS 44(D7), DC 10(D16), BUSY 4(D3), RST 38(D11), ENABLE 43(D6), no MISO.

## 6. Partial refresh is not available on the 2.9" BWRY panel
`USE_PARTIAL_EPAPER` is only defined for monochrome panels (SSD1680/81/83,
UC8179, ED103TC2) — there is no partial path in JD79667_Defines.h, so every
update is a full ~25 s sweep with multi-sweep flickering. Decided 2026-09-12:
clock-style content refreshes a few times a day at most; red/yellow reserved
for alerts and highlights, black carries text.

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

## 10. UTF-8 degree symbol on TFT_eSPI / Seeed_GFX
`epaper.drawString()` expects CP437 or single-byte ASCII. Standard UTF-8 literals
like `"%d°C"` embed the 2-byte sequence `\xC2\xB0`, causing TFT_eSPI to draw two
unintended glyphs (CP437 block/shade characters). In `verse_display.cpp`,
`dev_drawString` and `dev_measureText` decode `0xC2 0xB0` and render the degree
symbol as a proportional vector circle (`epaper.drawCircle`) at `(cursorX + 2*size, y + 2*size, r=size)`,
mirroring the host test harness.

## 11. Archiving Layout Screenshots for Lessons Learned
To preserve visual design iterations and prevent regressions, host-rendered layout
PNGs generated during testing are archived in
[`docs/images/`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/images/).
This tracks light, inverted, dark, and alert variants across portrait and landscape
orientations for future reference.



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
- **`tools/esp32s3_reset.py`**: Automatically cycles DTR/RTS serial control lines to release the native USB-Serial/JTAG download bootloader latch after flashing without requiring manual cable unplugging or button pressing.
