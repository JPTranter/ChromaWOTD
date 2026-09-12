#!/usr/bin/env python3
"""Preview a verse/weather fixture through the real layout engine — no flashing.

Examples
--------
    # Standard bundled fixture (tools/preview/sample_data.json)
    python tools/render_preview.py --fixture

    # Ad-hoc fixture, negative temperature + alert
    python tools/render_preview.py --temp -2.5 --condition "Partly cloudy" \\
        --alert "Rain likely after 4 PM" --verse "Trust in the Lord with all your heart." \\
        --highlight "Lord" --reference "Proverbs 3:5-6"

    # Render a verse body kept in a file, and open the PNG when done
    python tools/render_preview.py --verse-file myverse.txt --open

The device has a single presentation (landscape, light theme); the 296x128 render
uses the same draw calls the firmware runs, so the PNG is what the panel refreshes to.
Renders land in `firmware/test/output/previews/` (gitignored, outside the docs/images
ledger).
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DIR = os.path.join(ROOT, "firmware", "test")
BUILD_DIR = os.path.join(TEST_DIR, "build")
DEFAULT_OUT_DIR = os.path.join(TEST_DIR, "output", "previews")
FIXTURE = os.path.join(ROOT, "tools", "preview", "sample_data.json")

IS_WINDOWS = os.name == "nt"
EXE_NAME = "layout_render.exe" if IS_WINDOWS else "layout_render"
# Ninja/GCC on Windows drops the binary in the build root or a config subdir.
EXE_CANDIDATES = (
    os.path.join(BUILD_DIR, EXE_NAME),
    os.path.join(BUILD_DIR, "Release", EXE_NAME),
    os.path.join(BUILD_DIR, "Debug", EXE_NAME),
)


def find_exe():
    for path in EXE_CANDIDATES:
        if os.path.exists(path):
            return path
    return None


def run(cmd, cwd=ROOT, capture=False):
    return subprocess.run(cmd, cwd=cwd, capture_output=capture, text=True)


def ensure_built(rebuild):
    """Configure (first run) and build the layout_render target."""
    if not os.path.exists(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        print("[0/3] Configuring CMake build directory...")
        if run(["cmake", "-S", TEST_DIR, "-B", BUILD_DIR, "-G", "Ninja"]).returncode != 0:
            sys.exit("CMake configure failed")
    if rebuild or not find_exe():
        print("[1/3] Building layout_render...")
        if run(["cmake", "--build", BUILD_DIR, "--target", "layout_render"]).returncode != 0:
            sys.exit("Build failed")
    exe = find_exe()
    if not exe:
        sys.exit("layout_render binary not found after build")
    return exe


def fixture_args(path):
    """Turn the shared preview fixture into layout_render arguments."""
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    weather = data.get("weather", {})
    args = [
        "--verse", data.get("verse", ""),
        "--date", data.get("date", ""),
        "--reference", data.get("reference", ""),
        "--temp", str(weather.get("temp", 21)),
        "--condition", weather.get("condition", ""),
        "--icon", weather.get("icon", "partly"),
    ]
    if data.get("highlight"):
        args += ["--highlight", data["highlight"]]
    if weather.get("alert"):
        args += ["--alert", weather["alert"]]
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--verse", help="verse body text")
    parser.add_argument("--verse-file", help="file containing the verse body ('-' for stdin)")
    parser.add_argument("--highlight", help="phrase painted red")
    parser.add_argument("--reference", help="citation painted red")
    parser.add_argument("--date", help="header date string")
    parser.add_argument("--temp", type=float, help="temperature in Celsius (negatives fine)")
    parser.add_argument("--condition", help="condition label")
    parser.add_argument("--alert", help="alert banner text (red)")
    parser.add_argument("--icon", choices=["sun", "cloud", "rain", "partly", "0", "1", "2", "3"],
                        help="weather icon")
    parser.add_argument("--out", help="output PNG path (default firmware/test/output/previews/preview.png)")
    parser.add_argument("--fixture", nargs="?", const=FIXTURE,
                        help="use the bundled sample_data.json fixture (optionally another path)")
    parser.add_argument("--rebuild", action="store_true", help="force a rebuild of layout_render")
    parser.add_argument("--open", action="store_true", help="open the PNG in the default viewer")
    args = parser.parse_args()

    exe = ensure_built(args.rebuild)

    cmd = [exe]
    if args.fixture:
        cmd += fixture_args(args.fixture)
    else:
        if args.verse:
            cmd += ["--verse", args.verse]
        if args.verse_file:
            cmd += ["--verse-file", args.verse_file]

    for flag, value in (("--highlight", args.highlight), ("--reference", args.reference),
                        ("--date", args.date), ("--condition", args.condition),
                        ("--alert", args.alert), ("--icon", args.icon),
                        ("--temp", args.temp)):
        if value is not None:
            cmd += [flag, str(value)]

    out = args.out
    if not out:
        os.makedirs(DEFAULT_OUT_DIR, exist_ok=True)
        out = os.path.join(DEFAULT_OUT_DIR, "preview.png")
    cmd += ["--out", out]

    print("[2/3] Rendering...")
    result = run(cmd, capture=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        sys.exit(result.returncode)

    print("[3/3] Done")
    if args.open and os.path.exists(out):
        if IS_WINDOWS:
            os.startfile(out)  # noqa: S606 - user-requested viewer launch
        elif sys.platform == "darwin":
            subprocess.run(["open", out], check=False)
        else:
            subprocess.run(["xdg-open", out], check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())