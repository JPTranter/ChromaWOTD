import serial
import time
import sys

def try_reset(dtr_first, rts_first):
    print(f"\n--- Testing reset with dtr={dtr_first}, rts={rts_first} ---")
    s = serial.Serial('COM13', 115200, timeout=0.1)
    
    # Release GPIO0 first (DTR=False) so it is pulled high by internal/external pull-up
    s.dtr = False
    s.rts = True  # reset chip
    time.sleep(0.1)
    s.rts = False # release reset
    time.sleep(0.1)
    s.dtr = False
    
    t0 = time.time()
    out = ""
    while time.time() - t0 < 3:
        if s.in_waiting:
            data = s.read(s.in_waiting).decode('utf-8', errors='replace')
            out += data
            sys.stdout.write(data)
            sys.stdout.flush()
        time.sleep(0.05)
    s.close()
    return out

# On ESP32-S3 with USB Serial/JTAG, esptool reset sequence:
def usb_jtag_reset():
    print("\n--- Testing USB-JTAG reset sequence ---")
    s = serial.Serial('COM13', 115200, timeout=0.1)
    s.dtr = False
    s.rts = False
    time.sleep(0.05)
    # Pull RTS high (in pyserial on Windows, RTS=True sets RTS pin low -> chip reset)
    s.rts = True
    s.dtr = False
    time.sleep(0.1)
    s.rts = False
    s.dtr = False
    time.sleep(0.1)
    t0 = time.time()
    while time.time() - t0 < 4:
        if s.in_waiting:
            data = s.read(s.in_waiting).decode('utf-8', errors='replace')
            sys.stdout.write(data)
            sys.stdout.flush()
        time.sleep(0.05)
    s.close()

usb_jtag_reset()
