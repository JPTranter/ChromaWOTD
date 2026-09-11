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
