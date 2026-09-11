# ChromaClock — Project Plan

## Concept

A 2.9" 4-colour ePaper display showing the time plus colour-coded at-a-glance
information (weather, temperature, reminders). Colour is the point: red/yellow
carry meaning (alerts, rain, heat), not decoration.

## Hardware

- Seeed 2.9" Quadruple Color ePaper, 128×296, JD79661, SPI (SKU 104990855)
- XIAO ePaper Display Board EE05 + XIAO ESP32-S3
- USB-powered initially; EE05 JST battery later

## Phases

### Phase 0 — Scaffold (current)
- [x] Repo structure, PlatformIO env for XIAO ESP32-S3
- [ ] First successful `pio run -e s3` build

### Phase 1 — Display bring-up
- [ ] Verify EE05 pin mapping (CS/DC/RST/BUSY) against the EE05 schematic
- [ ] Drive the panel with GxEPD2's JD79661 4-colour class
      (`GxEPD2_4C<GxEPD2_290_Z13c, ...>` — confirm exact class for JD79661)
- [ ] Full refresh test pattern in all 4 colours (~25 s expected)
- [ ] Measure a real full-refresh time and a partial-refresh attempt

### Phase 2 — Clock core
- [ ] NTP time sync over WiFi (SNTP), timezone handling
- [ ] Clock face layout using colour hierarchy
- [ ] Update cadence: ePaper refresh is slow; target minute-cadence partial or
      hourly full refresh — measure ghosting like eClock's §48

### Phase 3 — Weather
- [ ] WiFi + Open-Meteo (no API key) fetch, ArduinoJson parse
- [ ] Colour coding: red = rain/alert, yellow = temperature bands
- [ ] Failure states (no WiFi / no data) rendered honestly

### Phase 4 — Polish
- [ ] Config portal or hard-coded creds decision
- [ ] Enclosure sketch (panel outline 79 × 36.7 × 1.2 mm is identical to eClock's
      GDEY029T94 — reuse `eClock/docs/reference/enclosure-specs.md` measurements)
- [ ] Release tagging workflow mirroring eClock's tag-triggered release.yml

## Known unknowns

- Exact GxEPD2 class for the JD79661 2.9" quad-colour panel (likely
  `GxEPD2_290_Z13c`); verify in Phase 1.
- EE05 pin mapping on ESP32-S3 variant.
- Partial-refresh support quality on 4-colour panels (many only do full refresh
  or slow partials).
