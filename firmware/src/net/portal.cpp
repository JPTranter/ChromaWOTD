// portal.cpp — captive portal for first-boot / after-factory-reset configuration.
//
// Device-only (!CHROMAWOTD_HOST): it needs SoftAP + DNS catch-all + WebServer. The
// AP name/password generation itself lives in cc_portalMakeInfo(), which is pure and
// therefore also compiled on the host so the generation rule is unit-tested.
#include "net/portal.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Pure: AP identity + per-boot random password (host-testable)
// ---------------------------------------------------------------------------
void cc_portalMakeInfo(uint32_t macLow, uint32_t seed, PortalInfo* out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));

    // Name: last 3 MAC bytes in hex so two boards on one bench are distinguishable.
    snprintf(out->apName, sizeof(out->apName), "ChromaWOTD-%02X%02X%02X", (unsigned)((macLow >> 16) & 0xFF),
             (unsigned)((macLow >> 8) & 0xFF), (unsigned)(macLow & 0xFF));

    // Password: CC_PORTAL_APASS_LEN random DIGITS. Digits only because the user reads
    // this off a 4-colour ePaper panel and types it on a phone — 0/8, 1/7 and 5/6 at
    // 6px are far less ambiguous than letters, and there is no case to mis-enter.
    // The xorshift is adequate here: the password is regenerated every boot, is shown
    // on the device itself, and only needs to keep a passer-by out during the
    // (bounded) setup window — it is not a long-lived secret.
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (size_t i = 0; i < CC_PORTAL_APASS_LEN; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5; // xorshift32
        out->apPassword[i] = (char)('0' + (s % 10u));
    }
    out->apPassword[CC_PORTAL_APASS_LEN] = '\0';
}

// ---------------------------------------------------------------------------
// Shared: HTML escaping + the Wi-Fi picker renderer (host-testable)
// ---------------------------------------------------------------------------
namespace {

// Minimal HTML escaping for values echoed back into the form. Remote input is
// untrusted (S4): never emit it raw — and an SSID is remote input too, which is easy
// to overlook because it usually looks like an ordinary name.
void htmlEscape(const char* src, char* dst, size_t dstsz) {
    size_t o = 0;
    for (const char* p = src; p && *p && o + 6 < dstsz; p++) {
        switch (*p) {
        case '<':
            o += (size_t)snprintf(dst + o, dstsz - o, "&lt;");
            break;
        case '>':
            o += (size_t)snprintf(dst + o, dstsz - o, "&gt;");
            break;
        case '&':
            o += (size_t)snprintf(dst + o, dstsz - o, "&amp;");
            break;
        case '"':
            o += (size_t)snprintf(dst + o, dstsz - o, "&quot;");
            break;
        case '\'':
            o += (size_t)snprintf(dst + o, dstsz - o, "&#39;");
            break;
        default:
            dst[o++] = *p;
            break;
        }
    }
    dst[o] = '\0';
}

} // namespace

// Pure: the <option> list for the SSID picker (see portal.h for why the SSID is
// CHOSEN from a scan instead of typed).
size_t cc_portalRenderSsidOptions(const PortalScanEntry* entries, size_t count, const char* current,
                                  char* out, size_t outsz) {
    if (!out || outsz == 0)
        return 0;
    size_t o = 0;

    // An explicit empty first option: submitting without choosing is possible and
    // visible, and cc_configValidate() then rejects the empty SSID with a clear reason.
    o += (size_t)snprintf(out + o, outsz - o, "<option value=''>-- choose a network --</option>");

    for (size_t i = 0; i < count && o < outsz; i++) {
        if (!entries)
            break;
        // Worst case for htmlEscape is 6 output chars per input char ("&#39;").
        char esc[CC_PORTAL_SCAN_SSID_MAX * 6 + 1];
        htmlEscape(entries[i].ssid, esc, sizeof(esc));
        const bool sel = current && current[0] && strcmp(entries[i].ssid, current) == 0;
        o += (size_t)snprintf(out + o, outsz - o, "<option value='%s'%s>%s</option>", esc,
                              sel ? " selected" : "", esc);
    }
    out[outsz - 1] = '\0';
    return o;
}

// ---------------------------------------------------------------------------
// Device-only: SoftAP + DNS catch-all + web form
// ---------------------------------------------------------------------------
#ifndef CHROMAWOTD_HOST

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "chroma_version.h" // CHROMAWOTD_VERSION, recorded as configuredBy
#include "config/config.h"
#include "config/config_active.h"
#include "config/tz_map.h"
#include "draw/target.h"
#include "net/net.h" // NET_TEXT_MAX, shared text helpers
#include "text/glyphs.h"
#include "verse_display.h" // CC_WHITE/CC_BLACK/CC_RED/CC_YELLOW + target()

namespace {

WebServer g_server(80);
DNSServer g_dns;
bool g_saved = false;
DeviceConfig g_candidate; // parsed from the last submit, for repaint on error
PortalInfo g_info;        // live AP identity, kept so a repaint can show it

// Networks found by the scan taken just before the AP came up. Cached because the form
// is rebuilt on every request and a scan takes seconds. htmlEscape() lives in the
// shared section above, since the host-tested picker renderer uses it too.
PortalScanEntry g_scan[CC_PORTAL_SCAN_MAX];
size_t g_scanCount = 0;

String page(const char* statusLine) {
    char hostEsc[128];
    htmlEscape(g_candidate.hostname, hostEsc, sizeof(hostEsc));

    char latBuf[24], lonBuf[24];
    snprintf(latBuf, sizeof(latBuf), "%.4f", (double)g_candidate.latitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", (double)g_candidate.longitude);

    String h;
    h += F("<!doctype html><html><head><meta charset='utf-8'>");
    h += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    h += F("<title>ChromaWOTD setup</title><style>");
    h += F("body{font-family:system-ui,sans-serif;margin:1.2rem;max-width:34rem}");
    h += F("label{display:block;margin:.7rem 0 .2rem;font-weight:600}");
    h += F("input{width:100%;padding:.5rem;font-size:1rem;box-sizing:border-box}");
    h += F(".hint{color:#555;font-size:.85rem;font-weight:400}");
    h += F("button{margin-top:1rem;padding:.7rem 1.2rem;font-size:1rem}");
    h += F("p.err{background:#fde8e8;border:1px solid #b32025;padding:.6rem;color:#b32025}");
    h += F("</style></head><body><h1>ChromaWOTD setup</h1>");
    if (statusLine && statusLine[0]) {
        h += F("<p class='err'>");
        h += statusLine;
        h += F("</p>");
    }
    h += F("<form method='POST' action='/save'>");
    h += F("<label>Wi-Fi network</label>");
    // A PICKER, not free text. A hand-typed, case-sensitive SSID is how a device ends up
    // hunting a network that does not exist (LESSONS §50), and the list comes from the
    // device's OWN scan — so everything offered is genuinely in range. The manual field
    // below remains because a HIDDEN network never appears in a scan.
    h += F("<select name='ssid'>");
    {
        // Static, not stack: worst case is 12 options of fully-escaped 32-char names
        // (~6 KB), and the portal's handler runs on the Arduino loop task.
        static char opts[CC_PORTAL_SCAN_MAX * (CC_PORTAL_SCAN_SSID_MAX * 6 + 48) + 96];
        cc_portalRenderSsidOptions(g_scan, g_scanCount, g_candidate.ssid, opts, sizeof(opts));
        h += opts;
    }
    h += F("</select>");
    if (g_scanCount == 0) {
        h += F("<p class='hint'>The device found no networks in range. Check that your router is "
               "on, then type its name below.</p>");
    } else {
        h += F("<p class='hint'>Pick your network above, or type its name below if it is hidden.</p>");
    }
    h += F("<label>Or type the network name <span class='hint'>(hidden networks)</span></label>");
    h += F("<input name='ssid_manual' maxlength='32' autocapitalize='none' autocomplete='off' value=''>");
    h += F("<label>Wi-Fi password <span class='hint'>(leave blank for open networks)</span></label>");
    h += F("<input name='pass' type='password' maxlength='63' autocomplete='off'>");
    h += F("<label>Timezone</label>");
    // A PICKER, not free text. The previous free-text POSIX field accepted an
    // incomplete rule ("AEST-10AEDT" with no DST dates), which made newlib apply US DST
    // rules and put the clock an hour out — scheduling the 06:30 wake for 05:30 with no
    // visible symptom. An IANA name has no partial form, and the POSIX string is
    // derived on the device (config/tz_map.cpp).
    h += F("<select name='tz'>");
    for (int i = 0; i < cc_tzCount(); i++) {
        const char* name = cc_tzNameAt(i);
        if (!name)
            continue;
        h += F("<option value='");
        h += name;
        h += F("'");
        if (strcmp(name, g_candidate.timezone) == 0)
            h += F(" selected");
        h += F(">");
        h += name;
        h += F("</option>");
    }
    h += F("</select>");
    h += F("<label>Latitude</label><input name='lat' value='");
    h += latBuf;
    h += F("'>");
    h += F("<label>Longitude</label><input name='lon' value='");
    h += lonBuf;
    h += F("'>");
    h += F("<label>Device name <span class='hint'>(optional)</span></label>");
    h += F("<input name='host' value='");
    h += hostEsc;
    h += F("' maxlength='32'>");
    h += F("<label>Content</label><select name='mode'>");
    const uint8_t m = g_candidate.contentMode;
    h += F("<option value='0'");
    if (m == 0)
        h += F(" selected");
    h += F(">Time-based (verse mornings, word afternoons)</option>");
    h += F("<option value='1'");
    if (m == 1)
        h += F(" selected");
    h += F(">Verse of the Day only</option>");
    h += F("<option value='2'");
    if (m == 2)
        h += F(" selected");
    h += F(">Word of the Day only</option>");
    h += F("</select><button type='submit'>Save and restart</button></form>");
    h += F("<p class='hint'>Values are stored on the device. Long-press any button for 10s to reset.</p>");
    h += F("</body></html>");
    return h;
}

void handleRoot() {
    g_server.send(200, "text/html", page(nullptr));
}

// Redirect to the form. Every unknown path lands here, plus the specific probe URLs
// each OS uses — see the registrations in cc_portalRun() for why the explicit ones
// matter.
void redirectToForm() {
    // 302 (not 204/200-with-body) is what the detectors look for:
    //   Android  /generate_204        expects 204, so ANY other code means "portal"
    //   Apple    /hotspot-detect.html expects the word "Success"
    //   Windows  /ncsi.txt, /connecttest.txt expect "Microsoft Connect Test"
    // Returning a redirect satisfies all three: the body never matches, so the OS
    // concludes it is behind a captive portal and raises its sign-in sheet.
    g_server.sendHeader("Location", "http://192.168.4.1/", true);
    g_server.send(302, "text/plain", "");
}

void handleNotFound() {
    redirectToForm();
}

void handleSave() {
    char err[160] = "";
    DeviceConfig cfg = g_candidate; // keep prior values for fields left blank

    // The picker is the normal path; the manual field exists for hidden networks and WINS
    // when filled, so a name typed by hand is never silently overridden by a stale
    // <select> value still selected from an earlier render.
    if (g_server.hasArg("ssid_manual") && g_server.arg("ssid_manual").length() > 0) {
        cc_configCopy(cfg.ssid, sizeof(cfg.ssid), g_server.arg("ssid_manual").c_str());
    } else if (g_server.hasArg("ssid") && g_server.arg("ssid").length() > 0) {
        cc_configCopy(cfg.ssid, sizeof(cfg.ssid), g_server.arg("ssid").c_str());
    }
    if (g_server.hasArg("pass"))
        cc_configCopy(cfg.passphrase, sizeof(cfg.passphrase), g_server.arg("pass").c_str());
    if (g_server.hasArg("tz"))
        cc_configCopy(cfg.timezone, sizeof(cfg.timezone), g_server.arg("tz").c_str());
    if (g_server.hasArg("host"))
        cc_configCopy(cfg.hostname, sizeof(cfg.hostname), g_server.arg("host").c_str());
    if (g_server.hasArg("mode"))
        cfg.contentMode = (uint8_t)g_server.arg("mode").toInt();

    const bool latOk =
        g_server.hasArg("lat") && cc_configParseCoord(g_server.arg("lat").c_str(), &cfg.latitude, -90.0f, 90.0f);
    const bool lonOk =
        g_server.hasArg("lon") && cc_configParseCoord(g_server.arg("lon").c_str(), &cfg.longitude, -180.0f, 180.0f);
    if (!latOk)
        cc_configCopy(err, sizeof(err), "Latitude must be a number between -90 and 90.");
    else if (!lonOk)
        cc_configCopy(err, sizeof(err), "Longitude must be a number between -180 and 180.");

    g_candidate = cfg; // so a repaint shows what the user typed

    if (err[0] == '\0' && !cc_configValidate(cfg, err, sizeof(err))) {
        // validation failed: fall through with the message
    }

    if (err[0] != '\0') {
        Serial.printf("portal: rejected config (%s)\n", err);
        // Repaint the panel with the reason so a user at the device (not the phone)
        // also sees why nothing was saved. The AP name/password must be preserved,
        // so keep the live PortalInfo rather than passing null.
        cc_portalDrawScreen(g_info, PortalNotice::SaveRejected, err);
        g_server.send(400, "text/html", page(err));
        return;
    }

    cc_configCopy(cfg.configuredBy, sizeof(cfg.configuredBy), CHROMAWOTD_VERSION);
    if (!cc_configSaveToNvs(cfg)) {
        g_server.send(500, "text/html", page("Could not save to device storage."));
        return;
    }
    Serial.println("portal: configuration saved; restarting");
    g_saved = true;
    g_server.send(200, "text/html",
                  F("<html><body style='font-family:sans-serif;margin:2rem'>"
                    "<h1>Saved</h1><p>The device is restarting and will connect to your Wi-Fi. "
                    "You can disconnect from the ChromaWOTD network.</p></body></html>"));
}

} // namespace

// Scan for the networks the device can actually see. Cached by the caller so the form
// (rebuilt per request) does not rescan.
size_t cc_portalScanNetworks(PortalScanEntry* out, size_t maxOut) {
    if (!out || maxOut == 0)
        return 0;

    WiFi.mode(WIFI_STA);
    // show_hidden=false: a hidden AP reports no SSID, so it cannot be offered as a named
    // option anyway — the form's manual field is the path for those.
    const int found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

    size_t count = 0;
    for (int i = 0; i < found && count < maxOut; i++) {
        const String s = WiFi.SSID(i);
        if (s.length() == 0 || s.length() >= CC_PORTAL_SCAN_SSID_MAX)
            continue; // unnamed, or too long for the field
        bool dup = false;
        for (size_t k = 0; k < count && !dup; k++)
            dup = (strcmp(out[k].ssid, s.c_str()) == 0); // one SSID can span several BSSIDs
        if (dup)
            continue;
        cc_configCopy(out[count].ssid, sizeof(out[count].ssid), s.c_str());
        out[count].rssi = WiFi.RSSI(i);
        count++;
    }
    WiFi.scanDelete();

    // Strongest first: the network the user wants is usually the one with the best signal
    // where the device actually sits. Insertion sort — the list is at most 12 entries.
    for (size_t i = 1; i < count; i++) {
        const PortalScanEntry key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].rssi < key.rssi) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return count;
}

bool cc_portalRun(const PortalInfo& info, uint32_t timeoutMs) {
    g_info = info;
    // Seed the form from the current resolved configuration (the built-in defaults,
    // or whatever is already stored). Without this the zero-initialised g_candidate
    // rendered lat/lon as 0.0000 — and (0,0) is explicitly rejected by validation, so
    // a user who edited only the Wi-Fi fields could never save. It also pre-selects
    // the current timezone/content mode instead of leaving them blank.
    g_candidate = cc_configActive();

    // Scan BEFORE raising the AP. A scan needs the station interface, and doing it first
    // avoids AP+STA coexistence entirely (otherwise the AP has to follow the scanner onto
    // a channel). The form is then built from this cached list.
    g_scanCount = cc_portalScanNetworks(g_scan, CC_PORTAL_SCAN_MAX);
    Serial.printf("portal: scan found %u network(s) in range\n", (unsigned)g_scanCount);

    WiFi.mode(WIFI_AP);
    // Password required: an open AP would let anyone nearby reconfigure the device.
    if (!WiFi.softAP(info.apName, info.apPassword)) {
        Serial.println("portal: softAP failed");
        return false;
    }
    const IPAddress ip = WiFi.softAPIP();
    Serial.printf("portal: AP '%s' up, portal at http://%s/\n", info.apName, ip.toString().c_str());
#if defined(CHROMAWOTD_PORTAL_DEBUG)
    // Development-only: the password is shown on the panel anyway, so printing it in
    // an explicitly opt-in debug build adds no real exposure — but it is NOT printed
    // in a normal build, and this flag must never ship enabled. It exists so the
    // portal can be exercised end-to-end without a human reading the ePaper.
    Serial.printf("portal: DEBUG ap_password=%s\n", info.apPassword);
#endif

    // Catch-all DNS: point every lookup at us so the OS shows its sign-in sheet.
    g_dns.start(53, "*", ip);

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/save", HTTP_POST, handleSave);

    // Explicit handlers for the OS captive-portal probe URLs. The catch-all
    // (onNotFound) already redirects anything unknown, but some Android builds treat a
    // 404 on the probe path differently from a redirect, so handling the probe URLs
    // BY NAME is what makes the sign-in sheet appear reliably across devices:
    //   Android : /generate_204, /gen_204
    //   Apple   : /hotspot-detect.html, /library/test/success.html
    //   Windows : /ncsi.txt, /connecttest.txt, /redirect
    //   Firefox : /canonical.html, /success.txt
    static const char* kProbePaths[] = {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/hotspotdetect.html",
        "/library/test/success.html",
        "/ncsi.txt",
        "/connecttest.txt",
        "/redirect",
        "/canonical.html",
        "/success.txt",
    };
    for (const char* path : kProbePaths) {
        g_server.on(path, HTTP_GET, redirectToForm);
    }

    g_server.onNotFound(handleNotFound);
    g_server.begin();

    const uint32_t start = millis();
    while (!g_saved && (timeoutMs == 0 || millis() - start < timeoutMs)) {
        g_dns.processNextRequest();
        g_server.handleClient();
        delay(2);
    }

    g_server.stop();
    g_dns.stop();
    // Drop the SoftAP radio explicitly rather than relying on the caller's next
    // WiFi.mode() switch: a left-up AP is an unnecessary exposure and keeps the
    // radio drawing current.
    WiFi.softAPdisconnect(true);
    delay(200);
    return g_saved;
}

#endif // !CHROMAWOTD_HOST
