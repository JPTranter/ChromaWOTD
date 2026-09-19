# CHROMAWOTD — how the display has changed

The presentation is the product, so the record of how it got here is part of the design. This
file is the chronology: what each change was, why it was made, the render that was approved, and
the lesson it left behind. The renders are the archived copies in
[`images/history/`](images/history/) — the couple of dozen that were actually *decided from*,
rather than every screenshot ever taken.

**How to read the images.** The files in `images/` (the top level, not `history/`) are the
regression ledger: byte-compared on every `verify_all.py` run and rendered from the DEFAULT host
build, where the proportional font is compiled out. They therefore show the layout in the 5×7
fallback font, which is **not** what the panel looks like. The renders referenced below come from
the device-font path (`build-device`), so they show what the panel actually draws. That
distinction has bitten this project more than once — see the last row of the table in §7.

---

## 1. 2026-09-12 — five presentations, then one

Phase 1 shipped five layouts: portrait, portrait-inverted, landscape, landscape-inverted and
landscape-dark, with a theme/orientation dispatcher. They were collapsed to a **single light
landscape presentation** on the same day.

- **Why:** the 4-colour panel has no partial refresh — every update is a full ~25 s pigment
  sweep — so a "switch mode/theme" button was never going to be a usable interaction. Five
  layouts also meant every geometry change had to be made in five places, and they had already
  drifted (the dispatcher could not even reach "dark portrait" — REVIEW D3).
- **Evidence:** `layout_portrait.png`, `layout_landscape_inverted.png`, `layout_landscape_dark.png`,
  `layout_overflow_portrait.png`, `layout_alert_portrait.png` (all retained for reference).
- **Lesson:** removing options removed bugs. The collapse also deleted the cramped alert strip and
  most of the duplicated geometry constants.

## 2. 2026-09-12 — the font journey

Four attempts in one afternoon, each with a render:

| attempt | outcome |
|---|---|
| `font_convert.py` + FreeSans 8pt | **rejected** — crowded the weather column (`layout_landscape_freesans8_real.png`) |
| FreeSans 6pt | fits the layout (`layout_landscape_freesans6_real.png`), but the baseline sat wrong (`_freesans6_baseline.png`) |
| mono-hinted **Roboto 5.5pt** | **adopted** (`layout_landscape_roboto55_final.png`) — Roboto read best of the mono-hinted candidates |
| **auto-size ladder** 5 / 5.5 / 6pt | long verses step down instead of being cramped (`layout_landscape_autosize_4lengths.png`, `_autosize_boundary55.png`, `_autosize_med.png`) |

- **Why a ladder rather than one size:** a fixed size is wrong for something whose length varies
  from eleven characters to three hundred.
- **Lesson:** §27 (why 5.5pt, mono-hinted) and §38 — the ladder silently collapsed at one point
  because every candidate was measured while the *previous* font was still active, making the
  line count font-independent. The per-candidate font assignment is load-bearing.

## 3. 2026-09-12 — the weather column's own UI

The right-hand column got a `FORECAST`/`TOMORROW` caption with an underline divider, and the icon
**grew to fill the freed space** when there was no alert
(`layout_landscape_weather_reflow_before/after.png`, `..._alert_before/after.png`,
`..._rain_before/after.png`, `layout_landscape_label_forecast.png`,
`layout_landscape_label_tomorrow.png`).

- The four-state icon set was drawn and catalogued in `weather_icon_sheet.png`.
- **Known limitation, accepted:** only WMO ≥ 80 draws the Rain glyph, so Drizzle and Rain 61–67
  fall back to the plain Cloud (`weather_icon_drizzle_current_vs_rain.png`, LESSONS §35).
- **Retrospect:** this is the UI that §7 eventually removed. It was not wrong when it was built —
  it came out when the weather was treated as a co-equal half of the panel.

## 4. 2026-09-12 — Word of the Day arrives, and the caption line becomes dual

The bottom line went from "reference only" to a **dual caption**: the respelling pronunciation in
black at the left, the headword (or verse citation) in red at the right
(`layout_landscape_wotd_aligned.png`, `_wotd_definition_example.png`,
`_wotd_definition_example_evening.png`, `_wotd_evening_tomorrow.png`,
`_wotd_live_breviloquent_evening.png`).

- Two layouts were trialled before settling — the definition inline, versus the definition with the
  caption treatment (`_wotd_variant_a_inline.png`, `_wotd_variant_b_caption.png`).
- **Lesson:** the caption line's margins were then aligned to the body text
  (`layout_landscape_verse_aligned.png`), which is the kind of detail that reads as sloppiness when
  it is 4 px out and is invisible when it is right.

## 5. 2026-09-12 → 13 — structure, and stopping the UI from lying

Two changes that were not cosmetic but changed what the panel *said*:

- **Module split** (REVIEW D1–D6): the decoder and wrap math into `text/`, the icon into `draw/`,
  the backend behind a `DisplayTarget` interface, enums for icons/theme, named layout constants.
  No visible change by design — that is what the render ledger exists to prove.
- **No invented data** (`fix(content)`): a failed fetch used to render a canned verse and a
  defaulted **`0°C`** with full confidence. Now there is no bundled fallback at all — the body
  becomes an explicit "unavailable" screen and the weather column carried the reason as a red
  `OFFLINE:` / `PARTIAL:` alert. **A device that looks like it works when it does not is worse
  than one that looks broken** (LESSONS §44).

## 6. 2026-09-13 — the setup screen

The first-boot captive portal got its own UI: a Wi-Fi **QR code** so the network can be joined by
scanning, an 8-digit numeric AP password (numeric because it is read off a 4-colour panel by eye),
a left-aligned text block, the password rendered in the native 10pt font rather than a 2× scaled
5.5pt, and a **rejection screen** that repaints the panel with the reason when a form submit fails
validation (`layout_portal_setup_qr.png`, `_setup_10pt_password.png`, `_setup_rejected.png`,
`_portal_password_font_compare.png`).

## 7. 2026-09-19 — the verse-first redesign

The largest change since Phase 1, and the only one driven by a specific reader's need: the owner
reads this panel at 55 and said the text needed to be larger.

- **Removed:** the 70 px weather column (24% of the width), the `FORECAST`/`TOMORROW` caption, the
  vertical divider, the weather icon, and the mode title ("Verse of the Day" / "Word of the Day").
  The icon went further than the layout: the `WeatherIcon` enum, the WMO→icon mapping,
  `draw/weather_icon.*` and the sprite-sheet generator were deleted too, once it was clear
  nothing in the product drew them (LESSONS §61).
- **Header band = *what* you are reading:** the citation (`Proverbs 3:5-6`), or the word with its
  respelling, at 7pt — the line you actually glance at. The date sits right at the same size.
- **Body = the verse**, full panel width, auto-sized through a ladder now topping out at **10pt**.
- **Footer row = status/warnings bottom-left (red), weather as text bottom-right.**
- **Evidence:** `layout_landscape_fullwidth_verse.png`, `_fullwidth_word.png`,
  `_fullwidth_warning.png`, `_10pt_word_definition.png`, `_10pt_short_verse.png`.
- **Lessons:** §53 (the redesign and the defects found implementing it), §54 (the date was ISO on
  the device while every render showed `Fri, Sep 12` — a *provenance* bug, not a formatting one),
  §55 (the 10pt ceiling and what it does not fix).

---

## The through-line

1. **Fewer presentations, bigger type.** Five layouts → one; then a sidebar → none. Each removal
   made the remaining thing better.
2. **The evidence moved from opinion to measurement.** "Looks fine" produced the 8pt FreeSans
   rejection and the ladder; `cc_verseFontSize()` reporting its own choice through the engine is
   what makes font decisions reviewable rather than arguable.
3. **Renders are only evidence when they are built like the device.** The date format diverged for
   a whole design review because one path was handed a literal (§54), and the ledger renders show
   the fallback font rather than the shipped one.
4. **A display that lies is the worst failure mode.** The `0°C` fabrication and the canned verse
   (2026-09-13) were worse than any layout flaw, because they made a broken device look healthy.
