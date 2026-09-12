#!/usr/bin/env python3
"""
ChromaWOTD weather icon sprite sheet.

Renders all possible weather icons (0=Sun, 1=Cloud, 2=Rain, 3=Partly Cloudy)
exactly as `drawWeatherIcon` in firmware/src/draw/weather_icon.cpp draws them on the
4-colour BWRY panel, using the same Bresenham primitives as the host harness
(firmware/test/harness/canvas.cpp) and the same ePaper palette.

Output lands in firmware/test/output/weather_icon_sheet.png (gitignored, outside
the docs/images ledger) unless --output is given.

Usage:
    python tools/weather_icon_sheet.py                 # 1:1, 2x2 grid
    python tools/weather_icon_sheet.py --scale 3      # 3x nearest-neighbour view
    python tools/weather_icon_sheet.py --output out.png
"""

import argparse
import os

# ePaper palette (must match canvas.cpp CC_RGB)
CC_WHITE = 0
CC_BLACK = 1
CC_RED = 2
CC_YELLOW = 3
CC_RGB = {
    CC_WHITE: (245, 242, 234),
    CC_BLACK: (26, 26, 26),
    CC_RED: (179, 32, 37),
    CC_YELLOW: (232, 183, 26),
}

ICON_NAMES = {
    0: "Sun",
    1: "Cloud",
    2: "Rain",
    3: "Partly Cloudy",
}


class Canvas:
    """Host-harness-compatible canvas (Bresenham primitives, 0..3 semantic colours)."""

    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.px = [[CC_WHITE] * w for _ in range(h)]

    def _set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def draw_fast_hline(self, x, y, ln, c):
        if y < 0 or y >= self.h or ln <= 0:
            return
        for xx in range(max(x, 0), min(x + ln, self.w)):
            self._set(xx, y, c)

    def draw_fast_vline(self, x, y, ln, c):
        if x < 0 or x >= self.w or ln <= 0:
            return
        for yy in range(max(y, 0), min(y + ln, self.h)):
            self._set(x, yy, c)

    def draw_line(self, x0, y0, x1, y1, c):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self._set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def draw_circle(self, x0, y0, r, c):
        f = 1 - r
        ddF_x = 1
        ddF_y = -2 * r
        x = 0
        y = r
        self._set(x0, y0 + r, c)
        self._set(x0, y0 - r, c)
        self._set(x0 + r, y0, c)
        self._set(x0 - r, y0, c)
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            self._set(x0 + x, y0 + y, c)
            self._set(x0 - x, y0 + y, c)
            self._set(x0 + x, y0 - y, c)
            self._set(x0 - x, y0 - y, c)
            self._set(x0 + y, y0 + x, c)
            self._set(x0 - y, y0 + x, c)
            self._set(x0 + y, y0 - x, c)
            self._set(x0 - y, y0 - x, c)

    def fill_circle(self, x0, y0, r, c):
        self.draw_fast_vline(x0, y0 - r, 2 * r + 1, c)
        f = 1 - r
        ddF_x = 1
        ddF_y = -2 * r
        x = 0
        y = r
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            self.draw_fast_vline(x0 + x, y0 - y, 2 * y + 1, c)
            self.draw_fast_vline(x0 - x, y0 - y, 2 * y + 1, c)
            self.draw_fast_vline(x0 + y, y0 - x, 2 * x + 1, c)
            self.draw_fast_vline(x0 - y, y0 - x, 2 * x + 1, c)

    def fill_rect(self, x, y, rw, rh, c):
        for yy in range(y, y + rh):
            self.draw_fast_hline(x, yy, rw, c)


# Exact port of drawWeatherIcon(cx, cy, size, iconType). Keep the rain/partly-cloudy
# offsets proportional to size in lock-step with the firmware (== the 24px sheet).
def draw_weather_icon(c, cx, cy, size, iconType):
    r = size // 3
    if r < 3:
        r = 3
    cloudOutline = CC_BLACK
    cloudFill = CC_WHITE

    if iconType == 0:  # Sun
        c.fill_circle(cx, cy, r, CC_YELLOW)
        c.draw_circle(cx, cy, r, CC_YELLOW)
        r1 = r + 2
        r2 = size // 2
        c.draw_line(cx, cy - r1, cx, cy - r2, CC_YELLOW)
        c.draw_line(cx, cy + r1, cx, cy + r2, CC_YELLOW)
        c.draw_line(cx - r1, cy, cx - r2, cy, CC_YELLOW)
        c.draw_line(cx + r1, cy, cx + r2, cy, CC_YELLOW)
        d1 = int(r1 * 0.707)
        d2 = int(r2 * 0.707)
        c.draw_line(cx - d1, cy - d1, cx - d2, cy - d2, CC_YELLOW)
        c.draw_line(cx + d1, cy - d1, cx + d2, cy - d2, CC_YELLOW)
        c.draw_line(cx - d1, cy + d1, cx - d2, cy + d2, CC_YELLOW)
        c.draw_line(cx + d1, cy + d1, cx + d2, cy + d2, CC_YELLOW)
    elif iconType == 1:  # Cloud
        c.fill_circle(cx - size // 4, cy + size // 10, size // 5, cloudFill)
        c.draw_circle(cx - size // 4, cy + size // 10, size // 5, cloudOutline)
        c.fill_circle(cx + size // 5, cy + size // 10, size // 6, cloudFill)
        c.draw_circle(cx + size // 5, cy + size // 10, size // 6, cloudOutline)
        c.fill_circle(cx, cy - size // 10, size // 4, cloudFill)
        c.draw_circle(cx, cy - size // 10, size // 4, cloudOutline)
        c.fill_rect(cx - size // 4, cy - size // 10, size // 2, size // 3, cloudFill)
        c.draw_fast_hline(cx - size // 3, cy + size // 4, (size * 2) // 3, cloudOutline)
    elif iconType == 2:  # Rain
        # cyShift + drop x-offset scale with size (== shipped firmware at size 24)
        cyShift = cy - size // 6  # == cy-4 at size 24
        c.fill_circle(cx - size // 4, cyShift + size // 10, size // 5, cloudFill)
        c.draw_circle(cx - size // 4, cyShift + size // 10, size // 5, cloudOutline)
        c.fill_circle(cx + size // 5, cyShift + size // 10, size // 6, cloudFill)
        c.draw_circle(cx + size // 5, cyShift + size // 10, size // 6, cloudOutline)
        c.fill_circle(cx, cyShift - size // 10, size // 4, cloudFill)
        c.draw_circle(cx, cyShift - size // 10, size // 4, cloudOutline)
        c.fill_rect(cx - size // 4, cyShift - size // 10, size // 2, size // 3, cloudFill)
        c.draw_fast_hline(cx - size // 3, cyShift + size // 4, (size * 2) // 3, cloudOutline)
        # Rain drops (proportional; == 6/3/9 offsets at size 24)
        dx = size // 4
        dropTop = cyShift + size // 4 + 3
        dropBot = cyShift + size // 2 + 3
        c.draw_line(cx - dx, dropTop, cx - dx - 3, dropBot, CC_RED)
        c.draw_line(cx, dropTop, cx - 3, dropBot, CC_RED)
        c.draw_line(cx + dx, dropTop, cx + dx - 3, dropBot, CC_RED)
    else:  # Partly Cloudy (also default)
        ray = size // 8  # == 3 at size 24
        if ray < 3:
            ray = 3
        c.fill_circle(cx - size // 4, cy - size // 5, size // 4, CC_YELLOW)
        c.draw_line(
            cx - size // 4,
            cy - size // 5 - size // 4 - ray,
            cx - size // 4,
            cy - size // 5 - size // 4 - ray + 2,
            CC_YELLOW,
        )
        c.draw_line(
            cx - size // 4 - size // 4 - ray,
            cy - size // 5,
            cx - size // 4 - size // 4 - ray + 2,
            cy - size // 5,
            CC_YELLOW,
        )
        c.fill_circle(cx - size // 6, cy + size // 8, size // 5, cloudFill)
        c.draw_circle(cx - size // 6, cy + size // 8, size // 5, cloudOutline)
        c.fill_circle(cx + size // 5, cy + size // 8, size // 6, cloudFill)
        c.draw_circle(cx + size // 5, cy + size // 8, size // 6, cloudOutline)
        c.fill_circle(cx + 2, cy - size // 12, size // 4, cloudFill)
        c.draw_circle(cx + 2, cy - size // 12, size // 4, cloudOutline)
        c.fill_rect(cx - size // 6, cy - size // 12, (size * 5) // 12, size // 3, cloudFill)
        c.draw_fast_hline(cx - size // 4, cy + size // 4, (size * 7) // 12, cloudOutline)


def bbox(img):
    w, h = len(img[0]), len(img)
    xs, ys, xe, ye = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if img[y][x] != CC_WHITE:
                if x < xs:
                    xs = x
                if x > xe:
                    xe = x
                if y < ys:
                    ys = y
                if y > ye:
                    ye = y
    return (xs, ys, xe, ye)


def render_icon(icon_type, size=24, pad=4):
    c = Canvas(size * 2, size * 2)
    cx, cy = size, size
    draw_weather_icon(c, cx, cy, size, icon_type)
    xs, ys, xe, ye = bbox(c.px)
    xs = max(0, xs - pad)
    ys = max(0, ys - pad)
    xe = min(c.w - 1, xe + pad)
    ye = min(c.h - 1, ye + pad)
    return [row[xs : xe + 1] for row in c.px[ys : ye + 1]]


def to_png(img):
    from PIL import Image

    h = len(img)
    w = len(img[0])
    im = Image.new("RGB", (w, h), CC_RGB[CC_WHITE])
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = CC_RGB[img[y][x]]
    return im


def scale_nn(im, factor):
    from PIL import Image

    if factor == 1:
        return im
    return im.resize((im.width * factor, im.height * factor), resample=Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=1, help="nearest-neighbour upscale factor")
    ap.add_argument("--output", default=None, help="output PNG path")
    ap.add_argument("--grid", default="2x2", choices=["1x4", "2x2", "4x1"])
    args = ap.parse_args()

    from PIL import Image, ImageDraw, ImageFont

    icons = [render_icon(t, size=24) for t in range(4)]
    imgs = [to_png(i) for i in icons]

    cols, rows = (4, 1) if args.grid == "1x4" else ((2, 2) if args.grid == "2x2" else (1, 4))
    cell_h = max(im.height for im in imgs)
    pad = 10
    label_h = 18
    gap = 14
    margin = 16
    footer_h = 30

    try:
        font = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 13)
    except Exception:
        font = ImageFont.load_default()

    title_h = 24
    title = "ChromaWOTD \u2014 Weather Icons (drawWeatherIcon, 24px, 4-colour BWRY)"

    sheet = Image.new("RGB", (8, 8))  # temporary; real size computed after measuring
    tmp = ImageDraw.Draw(sheet)

    labels = [
        "%d  %s   (%dx%d ink)" % (i, ICON_NAMES[i], im.width, im.height)
        for i, im in enumerate(imgs)
    ]

    # Column width = widest of the icon box and its label, so no label collides
    # with the neighbouring column. All columns share the same width for a clean grid.
    col_w = 0
    for i, im in enumerate(imgs):
        box_w = im.width
        lbl_w = int(tmp.textlength(labels[i], font=font))
        col_w = max(col_w, box_w, lbl_w)
    col_w += 2 * pad  # breathing room inside each cell

    title_w = int(tmp.textlength(title, font=font))

    grid_w = cols * col_w + (cols - 1) * gap
    sheet_w = max(title_w, grid_w) + 2 * margin
    sheet_h = title_h + rows * (cell_h + label_h) + (rows) * gap + 2 * margin + footer_h

    sheet = Image.new("RGB", (sheet_w, sheet_h), CC_RGB[CC_WHITE])
    d = ImageDraw.Draw(sheet)
    d.text((margin, 8), title, fill=CC_RGB[CC_BLACK], font=font)

    for i, im in enumerate(imgs):
        r = i // cols
        col = i % cols
        left = margin + (sheet_w - 2 * margin - grid_w) // 2 + col * (col_w + gap)
        top = title_h + margin + r * (cell_h + label_h + gap) + gap
        ox = left + (col_w - im.width) // 2
        oy = top + (cell_h - im.height) // 2
        sheet.paste(im, (ox, oy))
        d.rectangle([left, top, left + col_w, top + cell_h], outline=CC_RGB[CC_BLACK])
        d.text(
            (left + (col_w - int(d.textlength(labels[i], font=font))) // 2, top + cell_h + 3),
            labels[i],
            fill=CC_RGB[CC_BLACK],
            font=font,
        )

    # palette legend (reserved footer region, below the grid)
    ly = sheet_h - margin - footer_h + 12
    lx = margin + 4
    d.text((lx, ly - 4), "Palette:", fill=CC_RGB[CC_BLACK], font=font)
    x = lx + 60
    for name, color in [
        ("white", CC_WHITE),
        ("black", CC_BLACK),
        ("red", CC_RED),
        ("yellow", CC_YELLOW),
    ]:
        d.rectangle([x, ly - 8, x + 16, ly + 8], fill=CC_RGB[color], outline=CC_RGB[CC_BLACK])
        d.text((x + 21, ly - 4), name, fill=CC_RGB[CC_BLACK], font=font)
        x += 60 + len(name) * 6

    if args.scale > 1:
        sheet = scale_nn(sheet, args.scale)

    out = args.output
    if not out:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        out = os.path.join(root, "firmware", "test", "output", "weather_icon_sheet.png")
    sheet.save(out)
    print("Sprite sheet written: %s" % out)
    print("  Icons: 4 (0=Sun, 1=Cloud, 2=Rain, 3=Partly Cloudy)")
    print(
        "  Sheet: %dx%dpx  (scale x%d, grid %s)"
        % (sheet.width, sheet.height, args.scale, args.grid)
    )


if __name__ == "__main__":
    main()
