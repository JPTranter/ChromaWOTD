#!/usr/bin/env python3
"""Retry-loop firmware upload: flash as soon as the device's serial port exists.

While the XIAO ESP32-S3 is deep-asleep its native USB Serial/JTAG port does not
exist (see README "Firmware Build & Flashing"), so a one-shot `pio run -t upload`
fails. The port returns on every wake — a button press or the next scheduled slot
— and the documented workaround is a retry loop. This is that loop.

    python tools/flash_when_awake.py [--seconds 300] [--env s3] [--port COM13]

Exit 0 when an upload succeeds, 2 if no port appeared / all attempts failed inside
the window, 3 if the build artifacts are missing (run a build first).

Why the pre-flight checks and the retry all exist (each learned the hard way):
  * A 30-minute retry loop once burned every attempt against a MISSING
    `.pio/build/<env>/` directory — while the loop's own hardcoded message blamed
    the serial port. Two fixes: verify the artifacts BEFORE waiting, and always
    print the tool's REAL error rather than a guessed cause.
  * One failed attempt is not a reason to give up. A flash can fail simply because
    the port vanished mid-transfer, and the next wake is seconds to minutes away.
  * The retry loop also retried a PERMANENT error: the upload command used
    `sys.executable -m platformio`, and the interpreter running this tool is not
    necessarily the one PlatformIO is installed under (on this host PlatformIO is
    Core 6.1.19 under C:\\Python314 and reached through the `pio` on PATH). 144
    attempts died on "No module named platformio" while the operator pressed the
    button over and over, and the wake window was spent. Hence `pio_cmd()` below —
    the same resolution `verify_all.py` uses — plus a --version pre-flight that
    fails before the port wait, and an abort on an environment error that no
    number of retries can fix.
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware"
PYTHON = sys.executable

# The pieces PlatformIO's uploader writes. Checked up front so a missing build is
# reported as a missing build, not as a port problem.
REQUIRED_ARTIFACTS = ("bootloader.bin", "partitions.bin", "firmware.bin")

# Failures that retrying cannot fix (the environment, not the device).
PERMANENT_MARKERS = ("No module named platformio", "not recognized as an internal or external command")


def pio_cmd():
    """PlatformIO invocation, resolved the same way tools/verify_all.py does it.

    `pio` is not guaranteed to be on PATH, and sys.executable is only correct if the
    interpreter running THIS tool is the one PlatformIO lives under — which it is not
    when the tool is started from a venv. Prefer a real `pio` executable; fall back to
    the module form only when there is none.
    """
    if shutil.which("pio"):
        return ["pio"]
    return [PYTHON, "-m", "platformio"]


def pio_works(cmd):
    """(ok, message) from `pio --version`, so an unusable environment is reported before
    the port wait instead of after it."""
    try:
        r = subprocess.run(cmd + ["--version"], capture_output=True, text=True)
    except OSError as exc:
        return False, str(exc)
    lines = (r.stdout + r.stderr).strip().splitlines()
    return r.returncode == 0, (lines[-1] if lines else "no output")


def ports():
    """COM ports currently present (Registry-backed, no pyserial dependency)."""
    try:
        out = subprocess.run(
            ["reg", "query", r"HKLM\HARDWARE\DEVICEMAP\SERIALCOMM"], capture_output=True, text=True
        ).stdout
    except OSError:
        return []
    return [line.split()[-1] for line in out.splitlines() if "REG_SZ" in line]


def missing_artifacts(env):
    build = FIRMWARE / ".pio" / "build" / env
    return [str(build / name) for name in REQUIRED_ARTIFACTS if not (build / name).is_file()]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--seconds", type=int, default=300, help="how long to wait (default 300)")
    ap.add_argument("--env", default="s3", help="PlatformIO environment (default s3)")
    ap.add_argument("--port", default=None, help="skip detection and use this port")
    args = ap.parse_args()

    # Pre-flight: fail immediately and loudly on a missing build, rather than
    # spinning for the whole window and mislabelling the cause.
    missing = missing_artifacts(args.env)
    if missing:
        print(f"ERROR: build artifacts for env '{args.env}' are missing:")
        for m in missing:
            print(f"   {m}")
        print(f"Nothing to upload. Build first, e.g.\n   {PYTHON} -m platformio run -e {args.env}")
        return 3

    deadline = time.time() + args.seconds
    attempts = 0

    # Environment pre-flight: an unusable PlatformIO makes every attempt fail instantly, so
    # say so BEFORE the operator starts pressing buttons at the device.
    pio = pio_cmd()
    ok, version = pio_works(pio)
    if not ok:
        print(f"ERROR: PlatformIO is not usable as {' '.join(pio)}:")
        print(f"   {version}")
        print("Nothing will be uploaded. Install/resolve PlatformIO first (on this host")
        print("`pio` lives in the Python314 user Scripts directory and IS on PATH).")
        return 3
    print(f"platformio: {version}")

    if args.port:
        chosen = args.port
        print(f"using --port {chosen}")
    else:
        print(f"waiting up to {args.seconds}s for a serial port to appear...")
        chosen = None
        while time.time() < deadline and not chosen:
            found = ports()
            if found:
                chosen = found[0]
                print(f"port {chosen} present")
            else:
                time.sleep(1)
        if not chosen:
            print("no serial port appeared in the window (device still asleep)")
            return 2

    cmd = pio + [
        "run",
        "-e",
        args.env,
        "-t",
        "upload",
        "--upload-port",
        chosen,
    ]

    while time.time() < deadline:
        attempts += 1
        print(f"[attempt {attempts}] uploading to {chosen} (this takes ~20s)...")
        result = subprocess.run(cmd, cwd=FIRMWARE, capture_output=True, text=True)
        if result.returncode == 0:
            print(f"flashed {chosen} OK on attempt {attempts}")
            return 0

        # Print what the tool ACTUALLY said. Never claim the port vanished unless the
        # output says so — a guessed cause here once cost half an hour of looping.
        combined = (result.stdout + result.stderr).strip().splitlines()
        print(f"  attempt {attempts} failed. Last lines of the real output:")
        for line in combined[-6:]:
            print(f"    {line}")
        joined = "\n".join(combined)
        if "port is busy or doesn't exist" in joined:
            print("  (the port went away mid-transfer - waiting for the next wake)")
        # A missing/broken PlatformIO will fail identically forever: stop rather than
        # burn the wake window that the operator is spending button presses on.
        if any(marker in joined for marker in PERMANENT_MARKERS):
            print("  ABORT: this is an environment error, not a transient upload failure.")
            print(f"  resolved PlatformIO as: {' '.join(pio)}")
            return 3
        time.sleep(1)

    print(f"gave up after {attempts} attempt(s) in {args.seconds}s")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
