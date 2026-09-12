#!/usr/bin/env python3
"""Measure (and assert) the alignment invariants of a rendered layout PNG.

Layout work kept coming down to the same manual question: *is this caption level
with the date, and does this rule line up with the body text margin?* Eyeballing
a 296x128 PNG is unreliable and pixel-scanning it by hand was repeated several
times. This tool does that scan and prints the answer.

Checked invariants (all measured on INK, not on pen origins):

  1. header alignment  — the weather-column caption's top ink row equals the
                         header title/date's top ink row (both are drawn at y=2;
                         a caption drawn lower reads as "touching" the rule).
  2. rule extent       — the caption rule spans exactly the body text block's
                         margins (left == right == kVerseMargin).
  3. body margin       — the body text's leftmost ink sits on that same margin.
  4. caption margin    — the left (pronunciation) caption's leftmost ink also
                         sits on that margin, despite '(' carrying a left bearing.

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
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IMAGE = os.path.join(ROOT, "docs", "images", "layout_landscape.png")

# Panel geometry — mirrors the constants in firmware/src/verse_display.cpp.
PANEL_W, PANEL_H = 296, 128
SPLIT_X = 226        # vertical divider
HEADER_H = 14        # yellow band height (rule sits at HEADER_H - 1)
MARGIN = 4           # kVerseMargin: body text block left/right margin
RULE_Y = 109         # kReferenceY

PAPER = (245, 242, 234)
YELLOW = (232, 183, 26)


def is_ink(p):
    """Dark pigment (the panel's black). Excludes paper and the yellow band fill."""
    return p != PAPER and p != YELLOW and p[0] < 120 and p[1] < 120 and p[2] < 120


def is_red(p):
    return p[0] > 140 and p[1] < 90 and p[2] < 90


def load(path):
    from PIL import Image
    return Image.open(path).convert("RGB")


def measure(path):
    img = load(path)
    W, H = img.size
    px = img.load()
    m = {"path": path, "size": (W, H)}

    def rows(x0, x1, y0, y1, pred):
        return [y for y in range(y0, y1) if any(pred(px[x, y]) for x in range(x0, x1))]

    def col_min(x0, x1, y0, y1, pred):
        for x in range(x0, x1):
            for y in range(y0, y1):
                if pred(px[x, y]):
                    return x
        return None

    # 1. header: left band (title+date) vs right column (weather caption)
    left_rows = rows(1, SPLIT_X - 1, 0, HEADER_H, is_ink)
    right_rows = rows(SPLIT_X + 1, W - 1, 0, HEADER_H, is_ink)
    m["header_left_top"] = left_rows[0] if left_rows else None
    m["header_right_top"] = right_rows[0] if right_rows else None

    # 2. caption rule extent (red horizontal run on the rule row)
    rule = [x for x in range(1, SPLIT_X) if is_red(px[x, RULE_Y])]
    m["rule"] = (min(rule), max(rule)) if rule else None

    # 3. body text left margin
    m["body_left"] = col_min(1, SPLIT_X, HEADER_H + 2, RULE_Y - 4, is_ink)

    # 4. left caption ink (black text below the rule)
    m["caption_left"] = col_min(1, SPLIT_X, RULE_Y + 2, PANEL_H, is_ink)
    m["caption_top"] = None
    cap_rows = [y for y in range(RULE_Y + 1, PANEL_H)
                if any(is_ink(px[x, y]) for x in range(1, SPLIT_X))]
    if cap_rows:
        m["caption_top"] = cap_rows[0]
    return m


def check(m):
    """Return a list of (ok, description) for each invariant."""
    out = []
    lt, rt = m["header_left_top"], m["header_right_top"]
    out.append((lt is not None and lt == rt,
                f"weather caption level with header date (left top y={lt}, right top y={rt})"))

    rule = m["rule"]
    if rule is None:
        out.append((True, "caption rule not present (nothing to align)"))
    else:
        left_ok = abs(rule[0] - MARGIN) <= 1
        right_ok = abs(rule[1] - (SPLIT_X - MARGIN)) <= 1
        out.append((left_ok and right_ok,
                    f"rule spans body margins (got x{rule[0]}..{rule[1]}, "
                    f"want x{MARGIN}..{SPLIT_X - MARGIN})"))

    body = m["body_left"]
    out.append((body is not None and abs(body - MARGIN) <= 1,
                f"body text left ink on margin (got x={body}, want x={MARGIN})"))

    cap = m["caption_left"]
    if cap is None:
        out.append((True, "no left caption in this render (nothing to align)"))
    else:
        out.append((abs(cap - MARGIN) <= 1,
                    f"left caption ink on body margin (got x={cap}, want x={MARGIN})"))

    # caption must sit BELOW the rule, not touch it
    if m["caption_top"] is not None:
        gap = m["caption_top"] - RULE_Y
        out.append((gap >= 1, f"caption sits below the rule (gap {gap} px)"))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", nargs="?", default=DEFAULT_IMAGE,
                        help="render to measure (default: docs/images/layout_landscape.png)")
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if an invariant fails")
    parser.add_argument("--all", action="store_true",
                        help="measure every PNG in docs/images (ignores the positional arg)")
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
        print(f"  header top ink   : left y={m['header_left_top']}  right y={m['header_right_top']}")
        print(f"  caption rule     : {('x%d..%d' % m['rule']) if m['rule'] else 'none'}")
        print(f"  body left ink    : x={m['body_left']}")
        print(f"  left caption ink : x={m['caption_left']} (top y={m['caption_top']})")
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
