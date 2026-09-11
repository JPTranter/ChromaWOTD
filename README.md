# ChromaClock — 4-colour ePaper clock + weather display

A 2.9" quadruple-colour (black/white/red/yellow) ePaper display showing time,
weather, and at-a-glance colour-coded information. Built on the lessons of the
sibling [eClock](../eClock) project.

## Hardware

| Part | Detail |
|---|---|
| Display | Seeed 2.9" Quadruple Color ePaper, 128×296 px, JD79661 driver IC, 24-pin FPC, SPI, 3.3 V |
| Driver board | XIAO ePaper Display Board EE05 |
| MCU | XIAO ESP32-S3 |
| Power | USB (battery via EE05 JST possible later) |

## Status

**Phase 0 — scaffold.** Firmware skeleton compiles; display bring-up next.

## Docs

- `docs/PROJECT_PLAN.md` — plan & phases
- `docs/STATUS.md` — current state (update every session)
- `docs/lessons/LESSONS_LEARNT.md` — inherited + new lessons

## Key inherited facts (from eClock)

- Display init on this panel family needs exact-power sequencing; draw everything
  inside the `firstPage()/nextPage()` paged loop.
- UF2 drag-and-drop is unreliable on this machine; use serial DFU (ESP32-S3 uses
  esptool over the native USB CDC instead).
- Python scripts on Windows: always `encoding='utf-8'`.
