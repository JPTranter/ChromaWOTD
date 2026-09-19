#!/usr/bin/env python3
"""Measure (and assert) the alignment invariants of a rendered layout PNG.

Layout work kept coming down to the same manual question: *does this content line up
with the margins and with the other things sharing its row?* Eyeballing a 296x128 PNG
is unreliable and pixel-scanning it by hand was repeated several times. This tool does
that scan and prints the answer.

Checked invariants (all measured on INK, not on pen origins):

  1. band extent   — the yellow header band ends exactly at HEADER_H, and the header
                     text sits INSIDE it (text escaping the band is the classic
                     "raised a font size and forgot the band" regression).
  2. body margin   — the body text's leftmost ink sits on the text margin (MARGIN).
  3. footer row    — the warning (left) and the weather text (right) start on the SAME
                     ink row, so the footer reads as one line. Skips when only one of
                     the two is present.
  4. right margin  — no ink reaches the panel's last two columns.

Usage
-----
    python tools/measure_layout.py                       # report on the golden render
    python tools/measure_layout.py --check               # exit 1 if an invariant fails
    python tools/measure_layout.py --check --all         # check every render in docs/images
    python tools/measure_layout.py firmware/test/output/previews/foo.png

Requires Pillow. If Pillow is missing the tool prints a SKIP notice and exits 0,
so it can be wired into verify_all.py without becoming a hard dependency.
"""

import argparse
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IMAGE = os.path.join(ROOT, "docs", "images", "layout_landscape.png")

# Panel geometry — mirrors the constants in firmware/src/verse_display.cpp.
PANEL_W, PANEL_H = 296, 128
HEADER_H = 18  # yellow band height (its separator rule sits at HEADER_H - 1)
MARGIN = 4  # kVerseMargin: text block left/right margin
FOOTER_Y0 = 110  # top of the footer row's scan window (kRowY is 115)
EDGE_GUARD = 2  # columns at the right edge that must stay clear

PAPER = (245, 242, 234)
YELLOW = (232, 183, 26)


def is_ink(p):
    """Dark pigment (the panel's black). Excludes paper and the yellow band fill."""
    return p != PAPER and p != YELLOW and p[0] < 120 and p[1] < 120 and p[2] < 120


def is_yellow(p):
    return p == YELLOW


def is_content(p):
    """Any pigment at all. The footer row holds a RED warning on the left and BLACK
    weather text on the right, so an ink-only predicate would silently miss the warning
    and report the row as empty."""
    return p != PAPER and p != YELLOW


def load(path):
    from PIL import Image

    return Image.open(path).convert("RGB")


def measure(path):
    img = load(path)
    W, H = img.size
    px = img.load()
    m = {"path": path, "size": (W, H)}

    def top_ink_row(x0, x1, y0, y1, pred=is_ink):
        for y in range(y0, y1):
            if any(pred(px[x, y]) for x in range(x0, x1)):
                return y
        return None

    def left_ink_col(y0, y1, x0=1, x1=None, pred=is_ink):
        for x in range(x0, x1 if x1 is not None else W):
            for y in range(y0, y1):
                if pred(px[x, y]):
                    return x
        return None

    # 1. where the yellow band actually ends (scan down the left edge)
    band_end = None
    for y in range(H):
        if not any(is_yellow(px[x, y]) for x in range(0, W, 8)):
            band_end = y
            break
    m["band_end"] = band_end

    # header text: must sit inside the band
    m["header_top"] = top_ink_row(1, W - 1, 0, HEADER_H)

    # 2. body text left ink (below the band, above the footer row)
    m["body_left"] = left_ink_col(HEADER_H + 2, FOOTER_Y0 - 2)

    # 3. footer row: the two items that share it must share a baseline
    mid = PANEL_W // 2
    m["footer_left_top"] = top_ink_row(1, mid, FOOTER_Y0, PANEL_H, is_content)
    m["footer_right_top"] = top_ink_row(mid, W - 1, FOOTER_Y0, PANEL_H, is_content)

    # 4. the right edge must stay clear. Scan BELOW the band: the band's separator rule
    # spans the full width by design, so counting it here would always fail.
    m["right_ink"] = left_ink_col(HEADER_H, H, PANEL_W - EDGE_GUARD, PANEL_W)
    return m


def check(m):
    """Return a list of (ok, description) for each invariant."""
    out = []

    band = m["band_end"]
    header_top = m["header_top"]
    band_ok = band is not None and abs(band - HEADER_H) <= 1
    header_inside = header_top is not None and band is not None and header_top < band
    out.append(
        (
            band_ok and header_inside,
            f"header band ends at y={band} (want {HEADER_H}) and text sits inside it "
            f"(top ink y={header_top})",
        )
    )

    body = m["body_left"]
    out.append(
        (
            body is not None and abs(body - MARGIN) <= 1,
            f"body text left ink on margin (got x={body}, want x={MARGIN})",
        )
    )

    fl, fr = m["footer_left_top"], m["footer_right_top"]
    if fl is None or fr is None:
        out.append((True, "only one item on the footer row (nothing to level)"))
    else:
        out.append(
            (
                abs(fl - fr) <= 1,
                f"warning and weather share a footer baseline (left top y={fl}, right top y={fr})",
            )
        )

    edge = m["right_ink"]
    out.append(
        (
            edge is None,
            f"right edge clear (first ink at x={edge}, none allowed before "
            f"x={PANEL_W - EDGE_GUARD})",
        )
    )
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "image",
        nargs="?",
        default=DEFAULT_IMAGE,
        help="render to measure (default: docs/images/layout_landscape.png)",
    )
    parser.add_argument("--check", action="store_true", help="exit non-zero if an invariant fails")
    parser.add_argument(
        "--all",
        action="store_true",
        help="measure every PNG in docs/images (ignores the positional arg)",
    )
    args = parser.parse_args()

    try:
        import PIL  # noqa: F401
    except ImportError:
        print("SKIP: Pillow not installed (pip install pillow) - alignment not checked")
        return 0

    if args.all:
        # Only layout renders share these invariants; docs/images also holds
        # non-layout artifacts (e.g. weather_icon_sheet.png, 506x216).
        targets = sorted(glob.glob(os.path.join(ROOT, "docs", "images", "layout_*.png")))
    else:
        targets = [args.image]

    failures = 0
    for path in targets:
        if not os.path.exists(path):
            print(f"FAIL: no such image: {path}")
            failures += 1
            continue
        m = measure(path)
        print(f"\n=== {os.path.basename(path)}  ({m['size'][0]}x{m['size'][1]}) ===")
        print(f"  band ends at      : y={m['band_end']} (text top ink y={m['header_top']})")
        print(f"  body left ink     : x={m['body_left']}")
        print(
            f"  footer row        : left top y={m['footer_left_top']}  "
            f"right top y={m['footer_right_top']}"
        )
        print(f"  right edge ink    : x={m['right_ink']}")
        print("  invariants:")
        for ok, desc in check(m):
            print(f"    {'PASS' if ok else 'FAIL'}: {desc}")
            if not ok:
                failures += 1

    print()
    if args.check and failures:
        print(f"FAILED: {failures} alignment invariant(s)")
        return 1
    print("OK: alignment invariants hold" if not failures else f"{failures} problem(s) reported")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
