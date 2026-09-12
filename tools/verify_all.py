#!/usr/bin/env python3
"""One-command verification of the ChromaWOTD firmware + host layout suite.

Runs, in order:
  1. firmware build  — `pio run -e s3` (optionally from a clean tree with --clean)
  2. host tests      — cmake configure/build + ctest
  3. render ledger   — md5 comparison of firmware/test/output against docs/images
  4. alignment        — measures caption/rule/margin invariants in the archived renders

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


def build_firmware(clean):
    step("1/4 firmware build")
    if clean:
        print("$ pio run -e s3 -t clean")
        run(["pio", "run", "-e", "s3", "-t", "clean"], cwd=FIRMWARE)
    print("$ pio run -e s3")
    result = run(["pio", "run", "-e", "s3"], cwd=FIRMWARE)
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
    step("2/4 host layout tests")
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


def check_alignment():
    step("4/4 layout alignment (tools/measure_layout.py --check)")
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
    step("3/4 render ledger (firmware/test/output vs docs/images)")
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
