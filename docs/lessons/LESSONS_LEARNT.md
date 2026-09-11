# ChromaClock — Lessons Learnt

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
   EE05 pin map (from EPaper_Board_Pins_Setups.h): SCLK D8, MOSI D10,
   CS 44(D7), DC 10(D16), BUSY 4(D3), RST 38(D11), ENABLE 43(D6), no MISO.
