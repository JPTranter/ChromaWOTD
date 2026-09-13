#!/usr/bin/env python3
"""End-to-end test of the first-boot captive portal, driven from the dev machine.

Sequence:
  1. wait for the device's serial port
  2. flash a MERGED, UNPROVISIONED image (built from a clone with no secrets.h) at 0x0,
     which also blanks the NVS region -> the device must come up in setup mode
  3. capture the boot log to read the AP name and the per-boot password
  4. join that SoftAP from this machine, GET the form, POST a valid configuration
  5. report; the device is expected to report success and restart into normal operation

Needs: tools/bench_watch.py (for reset-free capture), a merged image path, and the
machine's Wi-Fi to be free to leave its current network for a minute.

Usage:
    python tools/test_portal_e2e.py --image <merged.bin>
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def ports():
    out = subprocess.run(
        ["reg", "query", r"HKLM\HARDWARE\DEVICEMAP\SERIALCOMM"], capture_output=True, text=True
    ).stdout
    return [ln.split()[-1] for ln in out.splitlines() if "REG_SZ" in ln]


def esptool():
    hits = sorted((Path.home() / ".platformio" / "packages").glob("tool-esptoolpy/esptool.py"))
    if not hits:
        sys.exit("esptool.py not found")
    return [sys.executable, str(hits[0])]


def flash(port, image):
    cmd = esptool() + [
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "460800",
        "write_flash",
        "0x0",
        str(image),
    ]
    print("$ " + " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout[-1200:])
    return r.returncode == 0


def capture_log(seconds):
    """Read serial WITHOUT asserting DTR/RTS (attaching normally resets the chip,
    which would restart the portal with a fresh password)."""
    r = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "bench_watch.py"),
            "--capture",
            "--seconds",
            str(seconds),
        ],
        capture_output=True,
        text=True,
    )
    return r.stdout + r.stderr


def wlan(cmd_args):
    return subprocess.run(["netsh", "wlan"] + cmd_args, capture_output=True, text=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", required=True, help="merged unprovisioned image")
    ap.add_argument("--wait", type=int, default=300, help="seconds to wait for the port")
    ap.add_argument("--ssid", default="JasonTestNet", help="SSID to submit to the form")
    ap.add_argument("--pass", dest="password", default="testpassphrase123")
    ap.add_argument("--latitude", default="-37.8528")
    ap.add_argument("--longitude", default="145.1633")
    ap.add_argument("--tz", default="AEST-10AEDT,M10.1.0,M4.1.0/3")
    args = ap.parse_args()

    print(f"[1/5] waiting up to {args.wait}s for the serial port...")
    deadline = time.time() + args.wait
    port = None
    while time.time() < deadline:
        p = ports()
        if p:
            port = p[0]
            print(f"      port {port} present")
            break
        time.sleep(1)
    if not port:
        print("      no port appeared - is the device awake? (press a button)")
        return 2

    print(f"[2/5] flashing {args.image} at 0x0 (blanks NVS -> unprovisioned boot)")
    if not flash(port, args.image):
        print("      flash failed")
        return 1
    print("      flashed; device is restarting into setup mode")

    # The portal stays up for CC_PORTAL_TIMEOUT_MS (10 min), so there is time to
    # capture the log without racing it.
    print("[3/5] capturing the boot log to read the AP name + per-boot password...")
    log = capture_log(90)
    name = re.search(r"portal: AP '([^']+)' up", log)
    pw = re.search(r"portal: DEBUG ap_password=(\S+)", log)
    if not name:
        print("      AP name not found in the log. Log tail:")
        print("\n".join(log.splitlines()[-25:]))
        return 1
    print(f"      AP name: {name.group(1)}")
    if not pw:
        print("      !! password not in the log. Either the debug flag is not set in this")
        print("         image, or the boot log was missed. Read it off the ePaper instead.")
        print("\n".join(log.splitlines()[-25:]))
        return 1
    ap_ssid, ap_pass = name.group(1), pw.group(1)
    print("      AP password captured (not echoed here)")

    print(f"[4/5] joining SoftAP '{ap_ssid}' ...")
    # A profile with the key in cleartext is required by netsh; it is written to a
    # temp file, used, then deleted.
    prof = ROOT / "firmware" / "test" / "output" / "portal-test-profile.xml"
    prof.parent.mkdir(parents=True, exist_ok=True)
    prof.write_text(
        '<?xml version="1.0"?>'
        '<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">'
        f"<name>{ap_ssid}</name><SSIDConfig><SSID><name>{ap_ssid}</name></SSID></SSIDConfig>"
        "<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>"
        "<MSM><security><authEncryption><authentication>WPA2PSK</authentication>"
        "<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>"
        f"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
        f"<keyMaterial>{ap_pass}</keyMaterial></sharedKey></security></MSM></WLANProfile>",
        encoding="utf-8",
    )
    subprocess.run(
        ["netsh", "wlan", "add", "profile", f"filename={prof}"], capture_output=True, text=True
    )
    prof.unlink(missing_ok=True)
    subprocess.run(["netsh", "wlan", "connect", f"name={ap_ssid}"], capture_output=True, text=True)
    time.sleep(12)
    iface = wlan(["show", "interfaces"])
    joined = ap_ssid in iface
    print(f"      joined: {joined}")

    print("[5/5] exercising the portal form...")
    base = "http://192.168.4.1"
    get = subprocess.run(["curl", "-sS", "-m", "20", base], capture_output=True, text=True)
    # The portal emits single-quoted attributes, so match that exact shape (an
    # earlier version of this check looked for double quotes and reported a
    # perfectly good form as missing).
    has_form = "name='ssid'" in get.stdout
    print(f"      GET /  -> {len(get.stdout)} bytes, form present: {has_form}")
    post = subprocess.run(
        [
            "curl",
            "-sS",
            "-m",
            "25",
            "-X",
            "POST",
            f"{base}/save",
            "--data-urlencode",
            f"ssid={args.ssid}",
            "--data-urlencode",
            f"pass={args.password}",
            "--data-urlencode",
            f"tz={args.tz}",
            "--data-urlencode",
            f"lat={args.latitude}",
            "--data-urlencode",
            f"lon={args.longitude}",
            "--data-urlencode",
            "host=PortalTestClock",
            "--data-urlencode",
            "mode=0",
        ],
        capture_output=True,
        text=True,
    )
    saved = "Saved" in post.stdout
    print(f"      POST /save -> saved: {saved}")
    print(f"      response head: {post.stdout[:120]!r}")

    print("\n--- RESULT ---")
    ok = joined and "name='ssid'" in get.stdout and saved
    print(
        "PASS: portal served the form and accepted a valid configuration"
        if ok
        else "FAIL: see the steps above"
    )
    print(
        "\nNOTE: the device should now restart and connect to "
        f"'{args.ssid}' (which does not exist, so expect an OFFLINE banner). "
        "Re-flash a provisioned image or reconfigure to restore normal operation."
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
