# Vendored: QR Code generator (Nayuki), C version

Upstream: https://github.com/nayuki/QR-Code-generator
Files: `qrcodegen.c`, `qrcodegen.h`
License: MIT (Copyright © Project Nayuki) — header retained in both files.

## Why vendored

The portal shows a Wi-Fi QR code so the user scans instead of typing an 8-digit
password, and the payload contains a **per-boot random** password, so the code must be
generated on the device at runtime. There is no QR encoder in the ESP32 Arduino core,
so one has to be bundled.

## Why the C implementation, not the C++ one

The C++ version (`cpp/qrcodegen.cpp`) signals errors with `throw`, and the ESP32
Arduino core compiles with exceptions disabled (`-fno-exceptions`), so it will not
build without changing the toolchain. The C version reports failure through `bool`
return values and uses caller-supplied buffers — no dynamic allocation beyond what the
caller provides, and no exceptions. It is also plain C99, so it compiles unchanged on
both the device and the host harness.

## Size / API notes

- `qrcodegen_encodeText()` needs two caller buffers: a temp buffer of
  `qrcodegen_BUFFER_LEN_MAX` and an output buffer of the same. At the maximum version
  (40) that is 2 x 3918 bytes, which is too much to put on the stack — the caller
  `drawPortalQrCode()` keeps them `static`.
- The portal payload is a short `WIFI:T:WPA;S:<ssid>;P:<pass>;;` string, so the chosen
  version is small (1-3) and the buffers are far from full.
