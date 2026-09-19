#!/usr/bin/env python3
"""Watch the device on the bench WITHOUT perturbing it.

Why this exists: `pio device monitor` asserts DTR/RTS when it attaches, which
**resets the ESP32-S3**. A retry-loop of monitor attaches therefore reboots the
board every cycle and manufactures a ~14 s "wake storm" that looks exactly like a
firmware bug (see LESSONS §34). Every wake/sleep question on this board needs one
of the two observation modes below instead.

Modes
-----
  --presence   Poll whether the serial port exists, without ever opening it. The
               ESP32-S3's native USB Serial/JTAG lives in the digital power
               domain, so the port VANISHES in deep sleep and reappears on wake.
               Port-presence polling is therefore a complete, non-invasive
               wake/sleep trace — and a period repeated every few seconds is a
               genuine wake storm, not a monitoring artifact.

  --capture    Wait for the port to appear (i.e. for the device to wake), then
               open it with DTR/RTS DEASSERTED and print what the firmware emits
               ("wake cause: 3 (button)", the sync log, "awake-status: ...").

Usage
-----
    python tools/bench_watch.py --presence --seconds 90
    python tools/bench_watch.py --capture --seconds 150   # then press a button
    python tools/bench_watch.py --port COM13 --capture

Requires pyserial (pip install pyserial).
"""

import argparse
import datetime
import sys
import time

# Last-resort port name, used ONLY when auto-detection finds nothing. That is the
# normal case here: the device deep-sleeps, so its USB port does not exist until it
# wakes. Both modes are built to WAIT for the port (`--capture` waits for it to
# appear, `--presence` treats its absence as the sleep signal), so resolution must
# never fail just because nothing is present yet. Override with --port COMx.
FALLBACK_PORT = "COM13"


def now():
    return datetime.datetime.now().strftime("%H:%M:%S")


def list_serials():
    from serial.tools import list_ports

    return [p.device for p in list_ports.comports()]


def present(port):
    """True when `port` exists. Never opens it, so the board is not reset."""
    return any(port in (d or "") for d in list_serials())


def mode_presence(port, seconds):
    print(
        f"watching {port} presence for {seconds:.0f}s "
        f"(never opening the port; no DTR/RTS, no reset)"
    )
    print("  the port is ABSENT while the device deep-sleeps and PRESENT while awake\n")
    start = time.time()
    last = None
    changes = 0
    while time.time() - start < seconds:
        state = present(port)
        if state != last:
            changes += 1
            print(f"  {now()}  -> {'PRESENT (awake)' if state else 'ABSENT (asleep)'}", flush=True)
            last = state
        time.sleep(1)
    print(f"\nstate changes in {seconds:.0f}s: {changes}")
    if changes > 2:
        print(
            "VERDICT: cycling repeatedly - genuine wake storm (or a monitor loop, "
            "but this mode never opens the port, so it is the firmware)"
        )
        return 1
    print("VERDICT: stable - the device slept and stayed asleep (or stayed awake)")
    return 0


def mode_capture(port, seconds):
    import serial

    print(f"waiting for {port} to appear (device asleep)... {now()}")
    start = time.time()
    while time.time() - start < seconds and not present(port):
        time.sleep(0.2)
    if not present(port):
        print(f"  {port} never appeared within {seconds:.0f}s")
        return 1
    print(f"  {now()}  {port} APPEARED - device woke; attaching (DTR/RTS deasserted)\n")

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.dtr = False  # set BEFORE open: avoids the reset-on-attach
    ser.rts = False
    ser.timeout = 1
    ser.open()
    ser.dtr = False
    ser.rts = False

    saw_button = False
    lines = 0
    while time.time() - start < seconds:
        try:
            raw = ser.readline()
        except Exception as exc:  # port vanishes when it sleeps
            print(f"  [{now()}] port closed ({type(exc).__name__}) - device slept")
            break
        if not raw:
            continue
        text = raw.decode("utf-8", "replace").rstrip()
        lines += 1
        print(f"  {now()} {text}")
        if "wake cause: 3" in text or "wake cause: 3 (button)" in text:
            saw_button = True
    try:
        ser.close()
    except Exception:
        pass

    print()
    if saw_button:
        print("VERDICT: button wake confirmed (wake_cause=3 / EXT1)")
        return 0
    if lines:
        print(f"VERDICT: captured {lines} line(s); no button wake seen in this window")
        return 0
    print("VERDICT: no output captured")
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--port",
        default=None,
        help="serial port (default: the first detected, e.g. COM13)",
    )
    parser.add_argument(
        "--seconds", type=float, default=90.0, help="observation window (default 90)"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--presence", action="store_true", help="poll port presence (no reset)")
    group.add_argument(
        "--capture", action="store_true", help="wait for wake, then read the boot log"
    )
    args = parser.parse_args()

    try:
        import serial  # noqa: F401
    except ImportError:
        print("ERROR: pyserial not installed (pip install pyserial)")
        return 2

    # Resolve the port. Auto-detection is preferred but must NOT be required: while
    # the device deep-sleeps its port does not exist, and this tool exists to observe
    # precisely that state. Requiring a present port at startup makes both modes fail
    # whenever the device is asleep — i.e. almost always, and always for `--capture`.
    if not args.port:
        detected = list_serials()
        if detected:
            args.port = detected[0]
            if len(detected) > 1:
                print(f"note: {len(detected)} ports found ({', '.join(detected)}); using {args.port}")
            else:
                print(f"using detected port {args.port}")
        else:
            args.port = FALLBACK_PORT
            print(
                f"no port present yet (the device is asleep - that is expected); "
                f"waiting for {args.port}. Pass --port COMx to name a different one."
            )

    if args.presence:
        return mode_presence(args.port, args.seconds)
    return mode_capture(args.port, args.seconds)


if __name__ == "__main__":
    sys.exit(main())
