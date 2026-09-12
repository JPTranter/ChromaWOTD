// net_host_curl.cpp — HOST-only implementation of the cc_fetchJson hook.
//
// Runs the system `curl` as a subprocess to fetch a URL into the caller's
// buffer, so the host harness can exercise the *real* network functions
// (cc_fetchWeather / cc_fetchVerse) against the live endpoints with TLS
// validation, without an ESP32. Compiled only under CHROMAWOTD_HOST; the
// device side is net_impl_esp32.cpp.

#include "net/net.h"
#include <cstdio>
#include <cstring>

bool cc_fetchJson(const char* url, char* out, size_t outsz) {
    if (!url || !out || outsz < 2) return false;

    // Use pipe mode to grab stdout; curl exits non-zero on TLS/HTTP errors so
    // an invalid response is not mistaken for success. `-sS` keeps silent but
    // surfaces real errors. `--connect-timeout` bounds a hung network.
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -sS --connect-timeout 15 --max-time 30 \"%s\"", url);

    FILE* fp = popen(cmd, "r");
    if (!fp) return false;
    size_t total = 0;
    int c;
    while (total + 1 < outsz && (c = fgetc(fp)) != EOF) {
        out[total++] = (char)c;
    }
    int rc = pclose(fp);
    out[total] = '\0';
    return rc == 0 && total > 0;
}