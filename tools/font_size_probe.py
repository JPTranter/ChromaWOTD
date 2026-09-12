#!/usr/bin/env python3
"""Report which body font the verse-block auto-size ladder picks for given text.

Why this exists: the panel's verse body font is chosen at runtime by an auto-size
ladder (Roboto 6pt -> 5.5pt -> 5pt, largest that fits the 218x82px block), so "why
is today's message in the small font?" is answered by a measurement, not by
reading the source. `--live` pulls today's real content exactly as the device
does (same shared fetch path the host harness uses), which is how the flag-on
render for 2026-09-13 was confirmed to select 6pt.

The selector itself is `cc_verseFontSize()` in the firmware; this tool calls the
built layout_render binary's probing mode, so the answer ALWAYS comes from the
real layout engine and cannot drift from it.

Examples
--------
    python tools/font_size_probe.py --live          # today's actual content
    python tools/font_size_probe.py --verse-file v.txt
    python tools/font_size_probe.py --verse "Trust in the Lord with all your heart."
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DIR = os.path.join(ROOT, "firmware", "test")
BUILD_DIR = os.path.join(TEST_DIR, "build-device")  # device font path
EXE = os.path.join(BUILD_DIR, "layout_render.exe" if os.name == "nt" else "layout_render")

NAMES = {"Pt5": "Roboto 5pt", "Pt55": "Roboto 5.5pt", "Pt6": "Roboto 6pt"}


def ensure_built():
    if not os.path.exists(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        print(
            "$ cmake -S firmware/test -B firmware/test/build-device -G Ninja "
            "-DCHROMAWOTD_DEVICE_FONTS=ON"
        )
        subprocess.run(
            [
                "cmake",
                "-S",
                TEST_DIR,
                "-B",
                BUILD_DIR,
                "-G",
                "Ninja",
                "-DCHROMAWOTD_DEVICE_FONTS=ON",
            ],
            cwd=ROOT,
            check=True,
        )
    subprocess.run(["cmake", "--build", BUILD_DIR], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--verse", help="the text to measure")
    src.add_argument("--verse-file", help="file containing the text")
    src.add_argument(
        "--live",
        action="store_true",
        help="fetch today's real content (verse or word) like the device",
    )
    args = ap.parse_args()

    ensure_built()

    cmd = [EXE]
    if args.live:
        # --live is the VOTD path; --word-live the afternoon path. Pick by the same
        # clock rule the device uses (00:00-11:59 verse, 12:00-23:59 word).
        import datetime

        hour = datetime.datetime.now().hour
        mode = "--live" if hour < 12 else "--word-live"
        cmd.append(mode)
        which = "Verse of the Day" if hour < 12 else "Word of the Day"
        print(f"content mode: {which} (local hour {hour})")
    elif args.verse_file:
        cmd += ["--verse-file", args.verse_file]
    else:
        cmd += ["--verse", args.verse]

    cmd += ["--out", os.path.join(TEST_DIR, "output", "previews", "font_probe.png")]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return result.returncode or 1

    match = re.search(r"verse_font=(\w+)", result.stdout)
    if not match:
        print("layout_render did not report verse_font (rebuild layout_render)")
        return 1
    key = match.group(1)
    print(f"\n  auto-size selection: {NAMES.get(key, key)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
