#!/usr/bin/env python3
"""Release the ESP32-S3 native USB-Serial/JTAG bootloader latch after flashing.

When esptool resets the chip over the native USB Serial/JTAG port, the default
RTS/DTR sequence can leave strapping pin GPIO0 asserted LOW, so the ROM stays in
download mode (`boot:0x21 (DOWNLOAD(USB/UART0))`) instead of booting the sketch.
Toggling DTR/RTS releases it and the device boots from flash
(`boot:0x29 (SPI_FAST_FLASH_BOOT)`).

Usage:
    python tools/esp32s3_reset.py --port COM13
    python tools/esp32s3_reset.py --port COM13 --watch 5
"""
import argparse
import sys
import time

import serial

# pyserial on Windows inverts the DTR/RTS lines, so True here pulls the physical
# line low. Sequence: de-assert DTR (GPIO0 floats high via its pull-up), pulse the
# reset line, then release.
RESET_STEPS = (
    ("dtr", False, 0.05),
    ("rts", True, 0.10),
    ("rts", False, 0.10),
    ("dtr", False, 0.05),
)


def release_bootloader(port: str, baud: int = 115200, watch_s: float = 4.0) -> str:
    """Pulse DTR/RTS and return whatever the board prints while it boots."""
    with serial.Serial(port, baud, timeout=0.1) as ser:
        for line, value, pause in RESET_STEPS:
            setattr(ser, line, value)
            time.sleep(pause)

        captured = []
        deadline = time.time() + watch_s
        while time.time() < deadline:
            if ser.in_waiting:
                text = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                captured.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
            time.sleep(0.05)
    return "".join(captured)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True,
                        help="serial port of the board, e.g. COM13 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="monitor baud rate")
    parser.add_argument("--watch", type=float, default=4.0,
                        help="seconds of boot output to capture after the reset")
    args = parser.parse_args()

    try:
        release_bootloader(args.port, args.baud, args.watch)
    except serial.SerialException as exc:
        print(f"Could not open {args.port}: {exc}", file=sys.stderr)
        return 1

    print("\nReset pulse sent. The board should now be running its sketch.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
