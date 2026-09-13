// wifi_qr.h — build the Wi-Fi QR payload the setup screen displays.
//
// Format (the de-facto standard understood by Android, iOS 11+, and most scanners):
//
//     WIFI:T:WPA;S:<ssid>;P:<passphrase>;H:false;;
//
// The escaping rules matter more than they look: any of `\ ; , : "` inside a value
// MUST be backslash-escaped, and a passphrase containing a bare ';' or ':' would
// otherwise terminate the field early and produce a code that scans to the wrong
// credentials. SSIDs legitimately contain spaces, dashes and apostrophes, so this is
// not a theoretical concern.
//
// The builder is pure so it can be unit-tested, including the escaping, and so the
// host harness can render the same QR the device shows.
#pragma once

#include <cstddef>

// Longest payload we will emit. The portal payload is
// "WIFI:T:WPA;S:" (~12) + SSID (<=32) + ";P:" (3) + passphrase (<=63) + ";;" (2),
// with escaping potentially doubling the variable parts. 256 is comfortably above the
// worst case and keeps the QR version small (a larger payload means a denser code that
// the 296x128 panel cannot render legibly).
static constexpr size_t CC_QR_PAYLOAD_MAX = 256;

// Build the WIFI: payload for an SSID + passphrase into `out` (always NUL-terminated).
// `auth` is the security type token: "WPA" (also used for WPA2/WPA3 by convention),
// "WEP", or "nopass". Returns the payload length, or 0 if it would not fit — the
// caller must treat 0 as "do not draw a code" rather than drawing a truncated one,
// because a truncated payload scans as a different network.
size_t cc_qrBuildWifiPayload(char* out, size_t outsz, const char* ssid, const char* passphrase, const char* auth);
