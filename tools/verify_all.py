#!/usr/bin/env python3
"""One-command verification of the ChromaWOTD firmware + host layout suite.

Runs, in order:
  1. firmware build  — `pio run -e s3` (optionally from a clean tree with --clean)
  2. host tests      — cmake configure/build + ctest (5x7 fallback font path)
  3. device-font tests — the same suite with -DCHROMAWOTD_DEVICE_FONTS=ON, which is
                         the font path the firmware actually ships (LESSONS 38/39)
  4. render ledger   — md5 comparison of firmware/test/output against docs/images
  5. alignment       — measures caption/rule/margin invariants in the archived renders

Exit code is non-zero if any step fails, so this is CI-safe. `--fix` re-syncs the
archived renders (same as tools/regenerate_screenshots.py) instead of only reporting.

Examples
--------
    python tools/verify_all.py              # incremental firmware build + tests + ledger
    python tools/verify_all.py --clean      # full firmware rebuild (slower, ~30 s)
    python tools/verify_all.py --fix        # resync docs/images after a layout change
    python tools/verify_all.py --skip-firmware   # host-only iteration
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(ROOT, "firmware")
TEST_DIR = os.path.join(FIRMWARE, "test")
BUILD_DIR = os.path.join(TEST_DIR, "build")
OUTPUT_DIR = os.path.join(TEST_DIR, "output")
IMAGES_DIR = os.path.join(ROOT, "docs", "images")

# Library-side noise that a healthy build always emits.
BENIGN_WARNINGS = (
    "backslash and newline separated by space",
    "backslash-newline at end of file",
    "TOUCH_CS pin not defined",
)
INTERESTING_WARNING = re.compile(
    r"warning:.*(" + "|".join(re.escape(t) for t in BENIGN_WARNINGS) + r")", re.I
)


def md5(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def step(title):
    print(f"\n=== {title} ===")


def run(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def pio_cmd():
    """PlatformIO invocation. `pio` is not always on PATH (on this Windows host it
    is installed as a module in C:\\Python314), so fall back to `python -m platformio`."""
    if shutil.which("pio"):
        return ["pio"]
    return [sys.executable, "-m", "platformio"]


def build_firmware(clean):
    step("1/5 firmware build")
    pio = pio_cmd()
    if clean:
        print("$ " + " ".join(pio + ["run", "-e", "s3", "-t", "clean"]))
        run(pio + ["run", "-e", "s3", "-t", "clean"], cwd=FIRMWARE)
    print("$ " + " ".join(pio + ["run", "-e", "s3"]))
    result = run(pio + ["run", "-e", "s3"], cwd=FIRMWARE)
    output = result.stdout + result.stderr

    for line in output.splitlines():
        if line.startswith("Warning! Ignore unknown configuration option"):
            print(f"  !! {line.strip()}")
    warnings = [l.strip() for l in output.splitlines()
                if "warning:" in l.lower() and not INTERESTING_WARNING.search(l)]
    size = [l.strip() for l in output.splitlines() if l.strip().startswith(("RAM:", "Flash:"))]
    for line in size:
        print(f"  {line}")
    if warnings:
        print(f"  project warnings ({len(warnings)}):")
        for line in warnings[:10]:
            print(f"    {line}")

    ok = result.returncode == 0 and "SUCCESS" in output
    print(f"  {'PASS' if ok else 'FAIL'}: firmware build")
    return ok, len(warnings)


def run_host_tests():
    step("2/5 host layout tests (5x7 fallback font path)")
    if not os.path.exists(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        print("$ cmake -S firmware/test -B firmware/test/build -G Ninja")
        if run(["cmake", "-S", TEST_DIR, "-B", BUILD_DIR, "-G", "Ninja"]).returncode != 0:
            print("  FAIL: cmake configure")
            return False
    if run(["cmake", "--build", BUILD_DIR]).returncode != 0:
        print("  FAIL: cmake build")
        return False

    result = run(["ctest", "--test-dir", BUILD_DIR, "--output-on-failure"])
    tail = [l for l in result.stdout.splitlines() if "tests passed" in l or "Failed" in l]
    for line in tail:
        print(f"  {line.strip()}")
    ok = result.returncode == 0
    print(f"  {'PASS' if ok else 'FAIL'}: ctest")
    if not ok:
        print(result.stdout)
    return ok


def run_device_font_tests():
    """Run the layout suite again on the DEVICE font path.

    The firmware ships -DCHROMAWOTD_FONT_FREESANS=1, but the default host build
    compiles the fallback 5x7 path, so without this stage the proportional glyph,
    degree, measurement and auto-size code the panel actually executes has no
    coverage (LESSONS 38/39). CMake keeps this in its own build dir -- and the
    stage runs BEFORE the canonical suite's final ctest, because the layout tests
    write to fixed paths under firmware/test/output/ and would otherwise overwrite
    the 5x7 renders the ledger then compares against docs/images.
    """
    step("3/5 host layout tests (DEVICE font path, CHROMAWOTD_DEVICE_FONTS=ON)")
    build = os.path.join(TEST_DIR, "build-device")
    if not os.path.exists(os.path.join(build, "CMakeCache.txt")):
        print("$ cmake -S firmware/test -B firmware/test/build-device -G Ninja -DCHROMAWOTD_DEVICE_FONTS=ON")
        if run(["cmake", "-S", TEST_DIR, "-B", build, "-G", "Ninja",
                "-DCHROMAWOTD_DEVICE_FONTS=ON"]).returncode != 0:
            print("  FAIL: cmake configure (device fonts)")
            return False
    if run(["cmake", "--build", build]).returncode != 0:
        print("  FAIL: cmake build (device fonts)")
        return False

    result = run(["ctest", "--test-dir", build, "--output-on-failure"])
    tail = [l for l in result.stdout.splitlines() if "tests passed" in l or "Failed" in l]
    for line in tail:
        print(f"  {line.strip()}")
    ok = result.returncode == 0
    print(f"  {'PASS' if ok else 'FAIL'}: ctest (device fonts)")
    if not ok:
        print(result.stdout)
    return ok


def restore_canonical_renders():
    """Re-run the canonical suite so firmware/test/output/ holds the 5x7 renders.

    The device-font stage above overwrites the layout PNGs (its tests write to the
    same fixed output/ paths), so without this the ledger would compare a 5x7
    baseline against device-font output and report every render as stale.
    """
    result = run(["ctest", "--test-dir", BUILD_DIR])
    ok = result.returncode == 0
    print(f"  {'PASS' if ok else 'FAIL'}: canonical renders restored")
    return ok


def check_alignment():
    step("5/5 layout alignment (tools/measure_layout.py --check)")
    result = run([sys.executable, os.path.join(ROOT, "tools", "measure_layout.py"),
                  "--check", "--all"])
    output = result.stdout + result.stderr
    # Surface only the problems and the verdict; a clean run is one line.
    problems = [l.strip() for l in output.splitlines() if l.strip().startswith("FAIL")]
    for line in problems:
        print(f"  ! {line}")
    skipped = any(l.strip().startswith("SKIP") for l in output.splitlines())
    ok = result.returncode == 0
    if skipped:
        print("  SKIP: alignment invariants (Pillow not installed)")
        return True
    print(f"  {'PASS' if ok else 'FAIL'}: alignment invariants hold")
    if not ok and not problems:
        print(output)
    return ok


def check_ledger(fix):
    step("4/5 render ledger (firmware/test/output vs docs/images)")
    if not os.path.isdir(OUTPUT_DIR):
        print("  FAIL: no renders found - run the tests first")
        return False

    rendered = {os.path.basename(p) for p in os.listdir(OUTPUT_DIR) if p.endswith(".png")}
    archived = {os.path.basename(p) for p in os.listdir(IMAGES_DIR) if p.endswith(".png")}
    problems = []

    for name in sorted(rendered):
        src = os.path.join(OUTPUT_DIR, name)
        dst = os.path.join(IMAGES_DIR, name)
        if name not in archived:
            problems.append(f"missing from docs/images: {name}")
        elif md5(src) != md5(dst):
            problems.append(f"stale in docs/images: {name}")

    for name in sorted(archived - rendered):
        problems.append(f"orphan in docs/images (no longer produced): {name}")

    if not problems:
        print(f"  PASS: {len(rendered)} renders match docs/images exactly")
        return True

    for problem in problems:
        print(f"  ! {problem}")

    if fix:
        for name in sorted(rendered):
            shutil.copy2(os.path.join(OUTPUT_DIR, name), os.path.join(IMAGES_DIR, name))
        for name in sorted(archived - rendered):
            os.remove(os.path.join(IMAGES_DIR, name))
        print("  FIXED: docs/images resynced - re-run to confirm clean")
        return True

    print("  FAIL: run `python tools/verify_all.py --fix` (or tools/regenerate_screenshots.py)")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--clean", action="store_true", help="clean firmware rebuild")
    parser.add_argument("--skip-firmware", action="store_true", help="host tests + ledger only")
    parser.add_argument("--skip-tests", action="store_true", help="firmware build only")
    parser.add_argument("--fix", action="store_true", help="resync docs/images instead of failing")
    args = parser.parse_args()

    results = {}
    if not args.skip_firmware:
        ok, _ = build_firmware(args.clean)
        results["firmware"] = ok
    if not args.skip_tests:
        results["tests"] = run_host_tests()
        results["tests_device_fonts"] = run_device_font_tests()
        # The device-font stage overwrites output/*.png, so restore the canonical
        # renders BEFORE the ledger compares them and before the alignment stage
        # measures docs/images (which the ledger has just vouched for).
        results["canonical_renders"] = restore_canonical_renders()
        results["renders"] = check_ledger(args.fix)
        results["alignment"] = check_alignment()

    step("summary")
    for name, ok in results.items():
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    failed = [n for n, ok in results.items() if not ok]
    print("\nALL GREEN" if not failed else f"\nFAILED: {', '.join(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
