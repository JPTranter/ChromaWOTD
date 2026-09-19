// net_impl_esp32.cpp — Device-only network implementation for CHROMAWOTD.
//
// The device side of the cc_fetchJson hook: WiFi connect + WiFiClientSecure with
// TLS root-CA validation (S1: never setInsecure) + HTTPClient, then a small
// ArduinoJson parse that fills the same structs net.cpp defines. Behaviours
// (quote/entity decoding, WMO mapping, truncation) live in net.cpp so host and
// device stay identical; this file is deliberately thin and is NOT compiled on
// the host (guarded by !CHROMAWOTD_HOST).

#ifndef CHROMAWOTD_HOST
#include "net/net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#ifdef CHROMAWOTD_MDNS
#include <ESPmDNS.h> // only pulled in when mDNS is enabled (heavy component)
#endif
#include <ArduinoJson.h>

// Configuration (NVS -> built-in defaults). NOTE: there is deliberately NO secrets.h
// include here — credentials come only from the device's setup portal and live in
// NVS, never from a source file, so a locally-built image can never contain them.
#include "config/config.h"
#include "config/config_active.h"
#include "config/config_compiletime.h"

// Root CAs the device trusts. These are TRUST ANCHORS (all self-signed), not the
// intermediates a server happens to be serving today:
//   - ISRG Root X1     (RSA,   valid to 2035) -> the Let's Encrypt hierarchy
//   - ISRG Root X2     (ECDSA, valid to 2040) -> the Let's Encrypt hierarchy
//   - Amazon Root CA 1 (RSA,   valid to 2038) -> www.biblegateway.com
//
// Why ROOTS and not intermediates: the previous build pinned the Let's Encrypt
// INTERMEDIATES the servers were serving at the time (YR2 for Open-Meteo, YE1 for
// Wordsmith). Those are not trust anchors — they are re-issued on a rotation
// schedule (the two pinned certificates expire 2028-09-02). The day Let's Encrypt
// served a chain under a different intermediate, EVERY fetch would have failed TLS
// verification until a firmware update — and this device has no OTA, so it would
// have meant a physical reflash. Trusting the root lets the server rotate its
// intermediate freely: the device validates whatever chain is presented, up to a
// root that is stable for a decade.
//
// Verified 2026-09-18 against the LIVE chains of all three hosts with
//   openssl verify -CAfile <this bundle> -untrusted <chain> <leaf>
// including the cross-signed intermediates (Root YR, Root YE) that a root-trusting
// client is served and needs in order to build the path.
static const char ROOT_CA_BUNDLE[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// One bundle for every host. A single bundle (rather than a per-host map) means a
// host that starts being served by a different hierarchy still validates, and there
// is no host->CA table to drift out of sync with what the servers actually send.
namespace {

// One throttled GET with TLS CA validation. Returns bytes written to out (the
// response body), or -1 on any failure. out is always NUL-terminated when >= 0.
int fetchHttpGet(const char* host, const char* path, char* out, size_t outsz) {
    WiFiClientSecure client;
    client.setCACert(ROOT_CA_BUNDLE); // S1: validate against the pinned roots, never setInsecure()
    if (!client.connect(host, 443))
        return -1;

    HTTPClient http;
    if (!http.begin(client, host, 443, path, true))
        return -1; // https=true
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return -1;
    }

    // getString() blocks until the whole body is read (respects Content-Length /
    // chunked encoding). A byte-at-a-time WiFiClient::available()/read() loop
    // was tried first and returned early/empty: available() can be transiently
    // false before all TCP segments have arrived, which surfaced downstream as
    // an empty buffer and an ArduinoJson "InvalidInput" parse error.
    String body = http.getString();
    size_t room = outsz > 0 ? outsz - 1 : 0;
    size_t n = body.length() < room ? body.length() : room;
    memcpy(out, body.c_str(), n);
    out[n] = '\0';
    http.end();
    return (int)n;
}

} // namespace

// Public hook: throttled fetch used by cc_fetchWeather / cc_fetchVerse.
// Splits the https URL into host + path, then does the CA-validated GET.
bool cc_fetchJsonThrottled(const char* url, char* out, size_t outsz) {
    // "https://" 8 bytes, then host up to the first '/', then path.
    if (strncmp(url, "https://", 8) != 0)
        return false;
    const char* host = url + 8;
    const char* slash = strchr(host, '/');
    char hostBuf[128];
    size_t hl = slash ? (size_t)(slash - host) : strlen(host);
    if (hl >= sizeof(hostBuf))
        return false;
    memcpy(hostBuf, host, hl);
    hostBuf[hl] = '\0';
    const char* path = slash ? slash : "/";
    int n = fetchHttpGet(hostBuf, path, out, outsz);
    return n > 0;
}

// --- Weather fetch (device): real HTTP + ArduinoJson parse -----------------
bool cc_fetchWeather(WeatherData* w, bool tomorrow) {
    char url[520];
    // timezone=auto, NOT the configured timezone. The device stores a POSIX TZ string
    // ("AEST-10AEDT,M10.1.0,M4.1.0/3") because that is what the local clock needs, but
    // Open-Meteo expects an IANA name and answers a POSIX string with HTTP 400 —
    // measured: timezone=AEST-10AEDT -> 400, timezone=Australia/Melbourne -> 200.
    // That made EVERY weather fetch fail silently behind a "No reading" column.
    // "auto" derives the zone from the lat/lon we already send, so the API needs no
    // timezone at all and the two uses stop being conflated.
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&timezone=auto&forecast_days=2",
             (double)cc_configActive().latitude, (double)cc_configActive().longitude);
    char json[4096];
    if (!cc_fetchJsonThrottled(url, json, sizeof(json)))
        return false;

    JsonDocument doc;
    if (deserializeJson(doc, json))
        return false;

    double temp = 0.0;
    int wmo = -1;
    JsonArray tmax = doc["daily"]["temperature_2m_max"];
    JsonArray codes = doc["daily"]["weather_code"];
    size_t dayIdx = tomorrow ? 1 : 0;
    if (tmax.size() <= dayIdx || codes.size() <= dayIdx)
        return false;
    temp = tmax[dayIdx].as<double>();
    wmo = codes[dayIdx].as<int>();
    if (wmo < 0)
        return false;

    static char cond[NET_TEXT_MAX];
    static char alert[NET_TEXT_MAX];
    bool needAlert = false;
    cc_wmoCondition(wmo, cond, sizeof(cond), &needAlert);
    w->temp = (float)temp;
    w->condition = cond;
    w->alert = needAlert ? (cc_alertFromWmo(wmo, alert, sizeof(alert)), alert) : nullptr;
    w->icon = (wmo == 0)             ? WeatherIcon::Sun
            : (wmo >= 1 && wmo <= 3) ? WeatherIcon::PartlyCloudy
            : (wmo >= 80)            ? WeatherIcon::Rain
                                     : WeatherIcon::Cloud;
    return true;
}

// --- Word of the Day (device): fetch the A.Word.A.Day page + shared parse ---
// The page is ~10 KB, so the buffer is static (BSS) rather than a stack frame —
// the sync task only owns 16 KB of stack.
bool cc_fetchWord(WordData* out) {
    static char html[16384];
    if (!cc_fetchJsonThrottled("https://wordsmith.org/words/today.html", html, sizeof(html)))
        return false;
    return cc_parseAwad(html, out);
}

// --- Verse fetch (device): real HTTP + ArduinoJson parse -------------------
bool cc_fetchVerse(VerseData* v) {
    char url[] = "https://www.biblegateway.com/votd/get/?format=json&version=NIV";
    char json[4096];
    if (!cc_fetchJsonThrottled(url, json, sizeof(json)))
        return false;

    JsonDocument doc;
    if (deserializeJson(doc, json))
        return false;
    JsonObject votd = doc["votd"];
    if (!votd)
        return false;

    static char text[NET_TEXT_MAX];
    static char ref[NET_TEXT_MAX];
    static char date[NET_TEXT_MAX];

    strncpy(text, votd["text"].as<const char*>() ? votd["text"].as<const char*>() : "", NET_TEXT_MAX - 1);
    text[NET_TEXT_MAX - 1] = '\0';
    strncpy(ref, votd["reference"].as<const char*>() ? votd["reference"].as<const char*>() : "", NET_TEXT_MAX - 1);
    ref[NET_TEXT_MAX - 1] = '\0';

    const char* y = votd["year"].as<const char*>() ? votd["year"].as<const char*>() : "";
    const char* m = votd["month"].as<const char*>() ? votd["month"].as<const char*>() : "";
    const char* d = votd["day"].as<const char*>() ? votd["day"].as<const char*>() : "";
    // Bound each component explicitly: they come from a remote payload, so the
    // compiler cannot prove "%s-%s-%s" fits and warns about truncation. Clamping
    // to the field widths the API actually uses keeps the format string honest
    // (date is "YYYY-MM-DD") without ever truncating real data.
    char yb[5] = "", mb[3] = "", db[3] = "";
    snprintf(yb, sizeof(yb), "%.4s", y);
    snprintf(mb, sizeof(mb), "%.2s", m);
    snprintf(db, sizeof(db), "%.2s", d);
    snprintf(date, sizeof(date), "%s-%s-%s", yb, mb, db);

    cc_htmlDecode(text);
    cc_stripLeadingBracket(text); // shared with host (net.cpp)

    v->verse = text;
    v->highlight = nullptr;
    v->reference = ref;
    v->date = date;
    return true;
}

// Network identity. The DHCP hostname is what the router's client list shows
// (option 12); the same name is registered with mDNS so the device also answers
// to "<name>.local" on the LAN. Override with -DCHROMAWOTD_HOSTNAME=\"foo\".
// (mDNS names resolve case-insensitively; keep it a single DNS label, no dots.)
// The default itself lives with the rest of the built-in configuration in
// config/config_compiletime.h — one definition, so it cannot drift from the value
// the DHCP request actually sends.

// Wi-Fi connect helper for main.cpp (kept here so network code stays out of the
// layout monolith). Returns err_t (0 == connected). Connects with a bounded
// timeout; never logs the passphrase. Returns an error immediately when no SSID is
// configured (nothing in NVS), so the caller runs the setup portal instead of
// attempting a connection with an empty name.
int cc_wifiConnect() {
    const DeviceConfig& cfg = cc_configActive();
    if (cfg.ssid[0] == '\0') {
        // Not provisioned: the caller runs the setup portal instead. Return an error
        // rather than attempting a connection with an empty SSID.
        return 1;
    }
    // Set the hostname BEFORE begin() so it is carried in the DHCP request and
    // the router can show "ChromaWOTD" instead of the default "espressif".
    WiFi.mode(WIFI_STA);
    const char* host = cfg.hostname[0] ? cfg.hostname : CHROMAWOTD_HOSTNAME;
    WiFi.setHostname(host);
    WiFi.begin(cfg.ssid, cfg.passphrase);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
        // Report WHY. "wifi FAILED" alone cannot distinguish a wrong passphrase from no
        // AP in range from a join that simply timed out, and those have completely
        // different causes. Never prints the credential itself.
        const wl_status_t st = WiFi.status();
        Serial.printf("sync: wifi FAILED, status=%d (", (int)st);
        switch (st) {
        case WL_NO_SSID_AVAIL:
            Serial.print("SSID not found in range");
            break;
        case WL_CONNECT_FAILED:
            Serial.print("connect failed - auth/association refused (wrong password shows here)");
            break;
        case WL_CONNECTION_LOST:
            Serial.print("connection lost");
            break;
        case WL_DISCONNECTED:
            Serial.print("disconnected / join timed out");
            break;
        case WL_IDLE_STATUS:
            Serial.print("idle - never left the station-idle state");
            break;
        case WL_NO_SHIELD:
            Serial.print("no wifi hardware");
            break;
        default:
            Serial.print("other");
            break;
        }
        Serial.println(")");
        return 1;
    }

#ifdef CHROMAWOTD_MDNS
    // Optional mDNS responder: registers the A record so "<name>.local" resolves
    // WITHOUT any server running. Measured cost ~24 KB flash / ~2 KB RAM, so it
    // is cheap — but the DHCP hostname above already makes the router show
    // "ChromaWOTD", which is all the default build needs. Enable with
    // -DCHROMAWOTD_MDNS=1 if you also want to reach it by name.
    if (MDNS.begin(host)) {
        Serial.printf("sync: mdns %s.local up\n", CHROMAWOTD_HOSTNAME);
    } else {
        Serial.println("sync: WARN mdns begin failed (hostname still set via DHCP)");
    }
#endif
    return 0;
}

#endif // !CHROMAWOTD_HOST
