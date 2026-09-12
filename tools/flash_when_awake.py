#!/usr/bin/env python3
"""Retry-loop firmware upload: flash as soon as the device's serial port exists.

While the XIAO ESP32-S3 is deep-asleep its native USB Serial/JTAG port does not
exist (see README "Firmware Build & Flashing"), so a one-shot `pio run -t upload`
fails. The port returns on every wake — a button press or the next scheduled slot
— and the documented workaround is a retry loop. This is that loop.

    python tools/flash_when_awake.py [--seconds 300] [--env s3]

Exit 0 when an upload succeeds, 2 if no port appeared inside the window.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware"
PYTHON = sys.executable


def ports():
    """COM ports currently present (Registry-backed, no pyserial dependency)."""
    try:
        out = subprocess.run(
            ["reg", "query", r"HKLM\HARDWARE\DEVICEMAP\SERIALCOMM"], capture_output=True, text=True
        ).stdout
    except OSError:
        return []
    return [line.split()[-1] for line in out.splitlines() if "REG_SZ" in line]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--seconds", type=int, default=300, help="how long to wait (default 300)")
    ap.add_argument("--env", default="s3", help="PlatformIO environment (default s3)")
    args = ap.parse_args()

    deadline = time.time() + args.seconds
    print(f"waiting up to {args.seconds}s for a serial port to appear...")
    seen = None
    while time.time() < deadline:
        found = ports()
        if found:
            seen = found[0]
            print(f"port {seen} present -> uploading")
            break
        time.sleep(1)

    if not seen:
        print("no serial port appeared in the window (device still asleep)")
        return 2

    cmd = [PYTHON, "-m", "platformio", "run", "-e", args.env, "-t", "upload", "--upload-port", seen]
    print("$ " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=FIRMWARE)
    if result.returncode != 0:
        print("upload failed (the port may have vanished mid-transfer; retry)")
        return result.returncode
    print(f"flashed {seen} OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
