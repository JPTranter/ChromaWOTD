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
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#ifdef CHROMAWOTD_MDNS
#include <ESPmDNS.h>   // only pulled in when mDNS is enabled (heavy component)
#endif
#include <ArduinoJson.h>

// Secrets are gitignored, so a fresh clone (and CI) has only secrets.h.example.
// Probe for the real file with __has_include and fall back to the example, which
// carries the same macro NAMES as commented-out placeholders — so the build
// always succeeds and the #ifndef defaults below fill in safe values.
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("secrets.h.example")
#include "secrets.h.example"
#endif

// config/config.h owns DeviceConfig; this TU resolves the values via
// cc_configActive(), populated by main.cpp at boot (NVS -> secrets.h -> built-in).
#include "config/config.h"
#include "config/config_active.h"
#include "config/config_compiletime.h"

// Root CAs the device trusts. Extracted from the live TLS chains (2026-09-12):
//   - Amazon Root CA 1            -> www.biblegateway.com (leaf + Amazon RSA 2048 M04)
//   - Let's Encrypt "YR2" inter.  -> api.open-meteo.com (leaf O=Let's Encrypt CN=YR2;
//                                   served by the new ISRG Root YR). We pin YR2 so
//                                   the bundle stays small; it chains to ISRG Root YR.
//   - Let's Encrypt "YE1" inter.  -> wordsmith.org (A.Word.A.Day), same new ECDSA
//                                   hierarchy (chains to ISRG Root YE / X2).
// These are deliberately NOT a system CA bundle: only the hosts we use.
static const char ROOT_CA_AMAZON[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIEkjCCA3qgAwIBAgITBn+USionzfP6wq4rAfkI7rnExjANBgkqhkiG9w0BAQsF
ADCBmDELMAkGA1UEBhMCVVMxEDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNj
b3R0c2RhbGUxJTAjBgNVBAoTHFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4x
OzA5BgNVBAMTMlN0YXJmaWVsZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1
dGhvcml0eSAtIEcyMB4XDTE1MDUyNTEyMDAwMFoXDTM3MTIzMTAxMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaOCATEwggEtMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/
BAQDAgGGMB0GA1UdDgQWBBSEGMyFNOy8DJSULghZnMeyEE4KCDAfBgNVHSMEGDAW
gBScXwDfqgHXMCs4iKK4bUqc8hGRgzB4BggrBgEFBQcBAQRsMGowLgYIKwYBBQUH
MAGGImh0dHA6Ly9vY3NwLnJvb3RnMi5hbWF6b250cnVzdC5jb20wOAYIKwYBBQUH
MAKGLGh0dHA6Ly9jcnQucm9vdGcyLmFtYXpvbnRydXN0LmNvbS9yb290ZzIuY2Vy
MD0GA1UdHwQ2MDQwMqAwoC6GLGh0dHA6Ly9jcmwucm9vdGcyLmFtYXpvbnRydXN0
LmNvbS9yb290ZzIuY3JsMBEGA1UdIAQKMAgwBgYEVR0gADANBgkqhkiG9w0BAQsF
AAOCAQEAYjdCXLwQtT6LLOkMm2xF4gcAevnFWAu5CIw+7bMlPLVvUOTNNWqnkzSW
MiGpSESrnO09tKpzbeR/FoCJbM8oAxiDR3mjEH4wW6w7sGDgd9QIpuEdfF7Au/ma
eyKdpwAJfqxGF4PcnCZXmTA5YpaP7dreqsXMGz7KQ2hsVxa81Q4gLv7/wmpdLqBK
bRRYh5TmOTFffHPLkIhqhBGWJ6bt2YFGpn6jcgAKUj6DiAdjd4lpFw85hdKrCEVN
0FE6/V1dN2RMfjCyVSRCnTawXZwXgWHxyvkQAiSr6w10kY17RSlQOYiypok1JR4U
akcjMS9cmvqtmg5iUaQqqcT5NJ0hGA==
-----END CERTIFICATE-----
)EOF";

static const char ROOT_CA_YR2[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIE2jCCAsKgAwIBAgIQTr0klH4k05SALYSlL9WzGTANBgkqhkiG9w0BAQsFADAu
MQswCQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjAe
Fw0yNTA5MDMwMDAwMDBaFw0yODA5MDIyMzU5NTlaMDMxCzAJBgNVBAYTAlVTMRYw
FAYDVQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQDEwNZUjIwggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQDZ0LxwBppqh84luqMerV/eeL/fXQ7mLQQv1Lnp
WKZbyvGpx6wh6AfnslAnF6ewTkcHA+gSOoBvm3Dfm06AuGiF+KRut4fAcowqnAQQ
CW98+QPP/eOv/wug7Iyk4NkOxf2I6g2f55T6nJoOTLFcukeRq80JGQEYan+dPFr9
OGUgQK2hGKgNkW87pappsOAuUJcroYhRt5uUis4qaZireiseu32gzDJNBAiKtsvd
6HX4v25bpkRNcS/B/Gtc9kVbUpD+2PLPxdei3Tim55k4tfAEXwD2qyiPTxrTNq6l
N+AMr5g2c1dNqkOTwjxeV6L5lpP1rGiYvLnRaPlOqyZRPW+5AgMBAAGjge4wgesw
DgYDVR0PAQH/BAQDAgGGMBMGA1UdJQQMMAoGCCsGAQUFBwMBMBIGA1UdEwEB/wQI
MAYBAf8CAQAwHQYDVR0OBBYEFEAVLSZ57TIgnt+ach3WMh+BDIEMMB8GA1UdIwQY
MBaAFN7nW2DQIm1AKH0/DQH+pLVStFGUMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEF
BQcwAoYWaHR0cDovL3lyLmkubGVuY3Iub3JnLzATBgNVHSAEDDAKMAgGBmeBDAEC
ATAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veXIuYy5sZW5jci5vcmcvMA0GCSqG
SIb3DQEBCwUAA4ICAQB0ZUQWZ9/Yn9COEpo+JfecMnB0h0vwDm/M66IqXqw3LoaL
mx9lZvRTeDIS67PUeI3yCA2W6PKRD0/FE/G57lOmS+Xy5AaaL00ICGOqjNcCaMWW
8o8nevHOd4i4lqgtznE/28QwlcdJyF8yBiWHpnyjhEpmNWJURgOCOg2xpwRMBCsj
MScqYPtOhBeuYQvSwAEeTML2Ukh6uGuX4E14q65Ja8cdjF5bAldnP1eE4FBaAwsZ
G2fOqqrKV03Y85Nw2btedP1AtliQuJZs/Jo/gXxXdc7LrH3McgnpnbTiAncX7yES
hP6kzQejllqMCIt52HOjxDGWafS7Xw+DKwqmH+Eqy8dcbOuag/1AYlQoKNVK3F5q
Hh6tEDiMqQcLIibGKteE6iHo4A/bIScbzrhXUYuism42ZYzmc48FMVIH3qy4L84E
TdAH2gtxw0PAhvRVXp8HP7wfngpzsN/8xOTpeRSbM4+Qbc56G6+Bifmv6sk1ieQb
NA3wJdl4DDUuQSV8hBgx6zoI1ZSGORprDFux7c6rhc77QZMSRrEgomBeklervEve
86ylWmZ3WWHV6RLMi8xNvjd71r4EPIGgY7BZU/VPBkq+uA7Gb6mbJnFgV43uh3xy
LRFgxIAphIukwTGSMZZR+AI+Qnp0BYTWovHXozOf3H8r6hozEoT02JHn0AeTfA==
-----END CERTIFICATE-----
)EOF";

static const char ROOT_CA_YE1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIICizCCAhGgAwIBAgIQXd1w3TH4AchcGGp6BLgK/jAKBggqhkjOPQQDAzAuMQsw
CQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZRTAeFw0y
NTA5MDMwMDAwMDBaFw0yODA5MDIyMzU5NTlaMDMxCzAJBgNVBAYTAlVTMRYwFAYD
VQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQDEwNZRTEwdjAQBgcqhkjOPQIBBgUr
gQQAIgNiAAQHZVB1/mimla2hfSurylScjPMZaOJXLz/NnAc2sylm8WDyhU9Ccp+z
ASQi5vSwGGJjSGklkD9fdPR8GpyDIOIjCEfrnbt/v+ZSEPLLEGbaM6EccDbN7p9x
teIm2Avf+ryjge4wgeswDgYDVR0PAQH/BAQDAgGGMBMGA1UdJQQMMAoGCCsGAQUF
BwMBMBIGA1UdEwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFLsgykcL/tflnPmPCSqj
jDdFsbzYMB8GA1UdIwQYMBaAFKPIJlqOoUzQNWP8myPIOq5W809WMDIGCCsGAQUF
BwEBBCYwJDAiBggrBgEFBQcwAoYWaHR0cDovL3llLmkubGVuY3Iub3JnLzATBgNV
HSAEDDAKMAgGBmeBDAECATAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veWUuYy5s
ZW5jci5vcmcvMAoGCCqGSM49BAMDA2gAMGUCMQDgjUEahFT/h3DRakqiPZpLvPgf
Zwkt6K2EOMmh1nvEzl83eMLYcod4GCl3b0J1Nn0CMBNYmEQJb4CEG5WoOe7aRn/L
VKu6saHmHEynI7ysIPd8zQsK1HdmhlHKlw9Z5GpGvA==
-----END CERTIFICATE-----
)EOF";

namespace {

// Pick the root CA for a host.
const char* caForHost(const char* host) {
    if (strstr(host, "open-meteo.com")) return ROOT_CA_YR2;
    if (strstr(host, "wordsmith.org"))  return ROOT_CA_YE1;
    return ROOT_CA_AMAZON;   // default: biblegateway
}

// One throttled GET with TLS CA validation. Returns bytes written to out (the
// response body), or -1 on any failure. out is always NUL-terminated when >= 0.
int fetchHttpGet(const char* host, const char* path, char* out, size_t outsz) {
    WiFiClientSecure client;
    client.setCACert(caForHost(host));   // S1: validate, never setInsecure()
    if (!client.connect(host, 443)) return -1;

    HTTPClient http;
    if (!http.begin(client, host, 443, path, true)) return -1;   // https=true
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return -1; }

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

}  // namespace

// Public hook: throttled fetch used by cc_fetchWeather / cc_fetchVerse.
// Splits the https URL into host + path, then does the CA-validated GET.
bool cc_fetchJsonThrottled(const char* url, char* out, size_t outsz) {
    // "https://" 8 bytes, then host up to the first '/', then path.
    if (strncmp(url, "https://", 8) != 0) return false;
    const char* host = url + 8;
    const char* slash = strchr(host, '/');
    char hostBuf[128];
    size_t hl = slash ? (size_t)(slash - host) : strlen(host);
    if (hl >= sizeof(hostBuf)) return false;
    memcpy(hostBuf, host, hl);
    hostBuf[hl] = '\0';
    const char* path = slash ? slash : "/";
    int n = fetchHttpGet(hostBuf, path, out, outsz);
    return n > 0;
}

// --- Weather fetch (device): real HTTP + ArduinoJson parse -----------------
bool cc_fetchWeather(WeatherData* w, bool tomorrow) {
    char url[520];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=%s&forecast_days=2",
        (double)cc_configActive().latitude, (double)cc_configActive().longitude,
        cc_configActive().timezone);
    char json[4096];
    if (!cc_fetchJsonThrottled(url, json, sizeof(json))) return false;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    double temp = 0.0;
    int wmo = -1;
    JsonArray tmax  = doc["daily"]["temperature_2m_max"];
    JsonArray codes = doc["daily"]["weather_code"];
    size_t dayIdx = tomorrow ? 1 : 0;
    if (tmax.size() <= dayIdx || codes.size() <= dayIdx) return false;
    temp = tmax[dayIdx].as<double>();
    wmo  = codes[dayIdx].as<int>();
    if (wmo < 0) return false;

    static char cond[NET_TEXT_MAX];
    static char alert[NET_TEXT_MAX];
    bool needAlert = false;
    cc_wmoCondition(wmo, cond, sizeof(cond), &needAlert);
    w->temp = (float)temp;
    w->condition = cond;
    w->alert = needAlert ? (cc_alertFromWmo(wmo, alert, sizeof(alert)), alert)
                         : nullptr;
    w->icon = (wmo == 0) ? WeatherIcon::Sun
            : (wmo >= 1 && wmo <= 3) ? WeatherIcon::PartlyCloudy
            : (wmo >= 80) ? WeatherIcon::Rain
            : WeatherIcon::Cloud;
    return true;
}

// --- Word of the Day (device): fetch the A.Word.A.Day page + shared parse ---
// The page is ~10 KB, so the buffer is static (BSS) rather than a stack frame —
// the sync task only owns 16 KB of stack.
bool cc_fetchWord(WordData* out) {
    static char html[16384];
    if (!cc_fetchJsonThrottled("https://wordsmith.org/words/today.html",
                               html, sizeof(html))) return false;
    return cc_parseAwad(html, out);
}

// --- Verse fetch (device): real HTTP + ArduinoJson parse -------------------
bool cc_fetchVerse(VerseData* v) {
    char url[] = "https://www.biblegateway.com/votd/get/?format=json&version=NIV";
    char json[4096];
    if (!cc_fetchJsonThrottled(url, json, sizeof(json))) return false;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonObject votd = doc["votd"];
    if (!votd) return false;

    static char text[NET_TEXT_MAX];
    static char ref[NET_TEXT_MAX];
    static char date[NET_TEXT_MAX];

    strncpy(text, votd["text"].as<const char*>() ? votd["text"].as<const char*>() : "", NET_TEXT_MAX - 1);
    text[NET_TEXT_MAX - 1] = '\0';
    strncpy(ref, votd["reference"].as<const char*>() ? votd["reference"].as<const char*>() : "", NET_TEXT_MAX - 1);
    ref[NET_TEXT_MAX - 1] = '\0';

    const char* y = votd["year"].as<const char*>() ? votd["year"].as<const char*>() : "";
    const char* m = votd["month"].as<const char*>() ? votd["month"].as<const char*>() : "";
    const char* d = votd["day"].as<const char*>()  ? votd["day"].as<const char*>()  : "";
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
    cc_stripLeadingBracket(text);   // shared with host (net.cpp)

    v->verse     = text;
    v->highlight = nullptr;
    v->reference = ref;
    v->date      = date;
    return true;
}

// Network identity. The DHCP hostname is what the router's client list shows
// (option 12); the same name is registered with mDNS so the device also answers
// to "<name>.local" on the LAN. Override with -DCHROMAWOTD_HOSTNAME=\"foo\".
// (mDNS names resolve case-insensitively; keep it a single DNS label, no dots.)
#ifndef CHROMAWOTD_HOSTNAME
#define CHROMAWOTD_HOSTNAME "ChromaWOTD"
#endif

// Wi-Fi connect helper for main.cpp (kept here so network code stays out of the
// layout monolith). Returns err_t (0 == connected). Connects with a bounded
// timeout; never logs the passphrase. When the secrets aren't configured (no
// WIFI_SSID) it returns immediately with an error so the device falls back to
// the offline path rather than failing to compile.
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
    if (WiFi.status() != WL_CONNECTED) return 1;

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

#endif  // !CHROMAWOTD_HOST
