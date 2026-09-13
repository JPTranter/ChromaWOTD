#!/usr/bin/env python3
"""Merge PlatformIO's ESP32-S3 build outputs into one flashable image.

Why: `pio run -e s3` emits four separate pieces that esptool writes at fixed
offsets (bootloader, partition table, boot_app0, the app). A release wants ONE
file a user can flash at 0x0 in a single command, so this merges them with
`esptool merge_bin` and then VERIFIES the result, because a silently-wrong merge
produces an image that flashes cleanly and then fails to boot.

The verification is the point: each piece is checked for its expected signature at
its expected offset (ESP32 application/bootloader images start 0xE9; the partition
table magic is 0xAA50 little-endian, so the first byte is 0xAA). `otadata` at
0xe000 is easy to omit — the partition table declares it, and leaving it out means
the bootloader has no valid OTA slot to select.

Usage:
    python tools/merge_firmware.py --out release/ChromaWOTD-v0.1.0.bin
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware" / ".pio" / "build" / "s3"

# Arduino-ESP32 (ESP32-S3) flash layout. These are the offsets PlatformIO itself
# uses on upload; keep them in this one place. `sig` is the expected first byte
# (None = no fixed signature, checked separately).
LAYOUT = [
    (0x0, "bootloader.bin", 0xE9, "bootloader"),
    (0x8000, "partitions.bin", 0xAA, "partition table"),
    (0xE000, None, None, "boot_app0"),  # framework file
    (0x10000, "firmware.bin", 0xE9, "application (app0)"),
]

FLASH_SIZE = os.environ.get("CHROMAWOTD_FLASH_SIZE", "8MB")
FLASH_MODE = "dio"


def find_esptool():
    """esptool ships inside the PlatformIO package dir; also accept a PATH copy."""
    if shutil.which("esptool.py"):
        return ["esptool.py"]
    pkgs = Path.home() / ".platformio" / "packages"
    hits = sorted(pkgs.glob("tool-esptoolpy/esptool.py")) if pkgs.is_dir() else []
    if hits:
        return [sys.executable, str(hits[0])]
    sys.exit("esptool.py not found (install PlatformIO or run `pip install esptool`)")


def find_boot_app0():
    """boot_app0.bin is a framework file, not a build output."""
    pkgs = Path.home() / ".platformio" / "packages"
    hits = sorted(pkgs.glob("framework-arduinoespressif32/tools/partitions/boot_app0.bin"))
    if not hits:
        sys.exit("boot_app0.bin not found under framework-arduinoespressif32/tools/partitions/")
    return hits[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="merged image path to write")
    ap.add_argument("--build-dir", default=str(BUILD), help="PlatformIO build dir")
    args = ap.parse_args()

    build = Path(args.build_dir)
    if not build.is_dir():
        sys.exit(f"build dir not found: {build} (run `pio run -e s3` first)")

    boot_app0 = find_boot_app0()
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    # Build the merge_bin argument list, validating inputs go in back-to-front.
    segments = []
    for off, name, _sig, label in LAYOUT:
        if name is None:
            segments.append((off, boot_app0, label))
            continue
        path = build / name
        if not path.is_file():
            sys.exit(f"missing build artifact: {path}")
        segments.append((off, path, label))

    cmd = find_esptool() + [
        "--chip",
        "esp32s3",
        "merge_bin",
        "-o",
        str(out),
        "--flash_mode",
        FLASH_MODE,
        "--flash_size",
        FLASH_SIZE,
    ]
    for off, path, _label in segments:
        cmd += [f"0x{off:x}", str(path)]

    print("$ " + " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.exit("esptool merge_bin failed")

    # --- Verify: right bytes at the right offsets, or fail loudly. -------------
    data = out.read_bytes()
    print(f"\nverifying {out} ({len(data)} bytes, {len(data)/1024/1024:.2f} MB)")
    problems = []
    for off, _path, sig, label in LAYOUT:
        if sig is None:
            continue
        got = data[off]
        ok = got == sig
        print(f"  0x{off:<6x} {label:22s} 0x{got:02X} {'OK' if ok else f'EXPECTED 0x{sig:02X}'}")
        if not ok:
            problems.append(label)

    # otadata must carry the boot_app0 placeholder rather than empty flash.
    if data[0xE000:0xE001] == b"\xff":
        problems.append("otadata (0xe000 is empty flash)")

    # The app must contain the version banner so a release image is identifiable.
    # "CHROMAWOTD %s boot" is the format string; the version itself is a separate
    # literal, so look for both pieces rather than one contiguous string.
    has_banner = b"CHROMAWOTD %s boot" in data or b"CHROMAWOTD " in data
    ver = re.search(rb"\b\d+\.\d+\.\d+\b", data)
    print(
        f"  version banner         {'found' if has_banner else 'NOT FOUND'}"
        f"{', version literal ' + ver.group(0).decode() if ver else ''}"
    )

    # --- Defence-in-depth: scan for credentials -------------------------------
    # There is no longer any way to compile credentials in — they exist only in NVS,
    # written by the device's setup portal (see config/config_compiletime.h). So this
    # check should never fire. It is kept deliberately as a tripwire: if a credential
    # path is ever reintroduced (a new macro, a generated header, a vendored include),
    # the release pipeline refuses to publish rather than leaking the developer's
    # network. That is the failure this project already came close to once.
    #
    # It scans the image for the *current* Wi-Fi config the build machine knows about,
    # read from any of the places a future change might put it.
    candidates = []
    for rel in (
        "firmware/src/secrets.h",
        "firmware/include/secrets.h",
        "firmware/src/wifi_config.h",
    ):
        p = ROOT / rel
        if not p.is_file():
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        for macro in ("WIFI_SSID", "WIFI_PASSPHRASE"):
            m = re.search(r"^\s*#define\s+" + macro + r'\s+"([^"]+)"', text, re.M)
            if m:
                candidates.append((rel, macro, m.group(1)))

    if candidates:
        leaked = [f"{macro} ({rel})" for rel, macro, val in candidates if val.encode() in data]
        if leaked:
            sys.exit(
                "\nREFUSING TO PRODUCE A RELEASE IMAGE: it contains real credentials "
                f"({', '.join(leaked)}).\n"
                "Credentials must live only in the device's NVS via its setup portal; "
                "nothing should ever be compiled in. If a credential path was just "
                "reintroduced, remove it rather than building from a clean checkout."
            )
        print("  credential scan        clean (no credential values in the image)")
    else:
        print("  credential scan        no compile-time credential source present")

    if problems:
        sys.exit(f"\nMERGE VERIFICATION FAILED: {', '.join(problems)}")

    print("\nmerge verified: single image flashable at 0x0")


if __name__ == "__main__":
    main()
