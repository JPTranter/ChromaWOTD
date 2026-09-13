// wifi_qr.cpp — Wi-Fi QR payload construction. See wifi_qr.h for the format rules.
#include "net/wifi_qr.h"

#include <cstring>

namespace {

// Append `src` to out[*pos], backslash-escaping the characters the WIFI: format
// reserves. Returns false if it would overflow.
bool appendEscaped(char* out, size_t outsz, size_t* pos, const char* src) {
    for (const char* p = src; p && *p; p++) {
        const bool needsEscape = (*p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"');
        if (*pos + (needsEscape ? 2u : 1u) + 1u > outsz)
            return false; // +1 for NUL
        if (needsEscape)
            out[(*pos)++] = '\\';
        out[(*pos)++] = *p;
    }
    return true;
}

// Append a literal (no escaping) — used for the fixed syntax characters.
bool appendRaw(char* out, size_t outsz, size_t* pos, const char* lit) {
    const size_t n = strlen(lit);
    if (*pos + n + 1u > outsz)
        return false;
    memcpy(out + *pos, lit, n);
    *pos += n;
    return true;
}

} // namespace

size_t cc_qrBuildWifiPayload(char* out, size_t outsz, const char* ssid, const char* passphrase, const char* auth) {
    if (!out || outsz == 0)
        return 0;
    // Every failure path leaves the buffer as an EMPTY string, not a partial payload.
    // A truncated payload scans as a different network with a different password, so
    // a caller that mis-handles the return value must not be able to draw one.
    out[0] = '\0';
    if (!ssid || !ssid[0])
        return 0; // without an SSID there is nothing to join

    const char* mode = (auth && auth[0]) ? auth : "WPA";
    size_t pos = 0;
    bool ok = true;

    ok = ok && appendRaw(out, outsz, &pos, "WIFI:T:");
    ok = ok && appendRaw(out, outsz, &pos, mode);
    ok = ok && appendRaw(out, outsz, &pos, ";S:");
    ok = ok && appendEscaped(out, outsz, &pos, ssid);
    ok = ok && appendRaw(out, outsz, &pos, ";");

    // An open network omits the P: field entirely; "nopass" is the conventional marker.
    if (passphrase && passphrase[0]) {
        ok = ok && appendRaw(out, outsz, &pos, "P:");
        ok = ok && appendEscaped(out, outsz, &pos, passphrase);
        ok = ok && appendRaw(out, outsz, &pos, ";");
    }

    ok = ok && appendRaw(out, outsz, &pos, "H:false;;");

    if (!ok) {
        out[0] = '\0'; // never hand back a partial payload
        return 0;
    }
    out[pos] = '\0';
    return pos;
}
