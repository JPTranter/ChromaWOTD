// net.cpp — network parse + mapping logic for CHROMAWOTD.
//
// Two layers live here:
//   (1) PURE, compiled for BOTH host and device:
//         - cc_wmoCondition / cc_alertFromWmo   (WMO code -> text + alert)
//         - cc_htmlDecode                       (HTML entity -> ASCII)
//       The device's net_impl_esp32.cpp calls these on the parsed JSON, so the
//       mapping is byte-identical host vs device.
//   (2) HOST-ONLY (under #ifdef CHROMAWOTD_HOST):
//         - the minimal JSON member extractor (no ArduinoJson on the host)
//         - cc_fetchWeather / cc_fetchVerse reading json fetched by the curl
//           hook. On the device these are defined in net_impl_esp32.cpp
//           (ArduinoJson); this TU deliberately defines NO device stubs here,
//           so there is exactly one definition per compile target and no ODR
//           clash.

#include "net/net.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

// Location defaults (Burwood East, Melbourne). Overridable per-build with -D
// flags; the device also sets them from secrets.h via net_impl_esp32.cpp. This
// TU intentionally does NOT include the gitignored secrets.h, so the host suite
// (and CI) builds green on a fresh checkout with no secrets file present.
#ifndef CHROMAWOTD_LATITUDE
#define CHROMAWOTD_LATITUDE -37.8528
#endif
#ifndef CHROMAWOTD_LONGITUDE
#define CHROMAWOTD_LONGITUDE 145.1633
#endif
#ifndef CHROMAWOTD_TIMEZONE
#define CHROMAWOTD_TIMEZONE "Australia/Melbourne"
#endif

// ---------------------------------------------------------------------------
// WMO code -> condition text + alert detection (SHARED host + device) -------
// ---------------------------------------------------------------------------
int cc_wmoCondition(int code, char* buf, size_t bufsz, bool* outNeedAlert) {
    *outNeedAlert = false;
    const char* s = "Cloudy";
    switch (code) {
        case 0:                                               s = "Clear"; break;
        case 1: case 2: case 3:                               s = "Partly cloudy"; break;
        case 45: case 48:                                     s = "Fog"; break;
        case 51: case 53: case 55: case 56: case 57:          s = "Drizzle"; break;
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82:                            s = "Rain"; break;
        case 71: case 73: case 75: case 77: case 85: case 86: s = "Snow"; break;
        case 95: case 96: case 99:                            s = "Thunderstorm"; *outNeedAlert = true; break;
        default:                                              s = "Cloudy"; break;
    }
    size_t need = strlen(s) + 1;
    if (bufsz < need) return -1;
    memcpy(buf, s, need);
    return (int)(need - 1);
}

int cc_alertFromWmo(int wmoCode, char* buf, size_t bufsz) {
    bool alert = false;
    char cond[16];
    cc_wmoCondition(wmoCode, cond, sizeof(cond), &alert);
    if (!alert) { if (bufsz < 1) return -1; buf[0] = '\0'; return 0; }
    const char* s = "Severe weather warning";
    size_t need = strlen(s) + 1;
    if (bufsz < need) return -1;
    memcpy(buf, s, need);
    return (int)(need - 1);
}

// ---------------------------------------------------------------------------
// HTML entity decoding (SHARED host + device). In-place; every entity maps to
// an equal-or-shorter ASCII char, so the buffer never outgrows itself.
// ---------------------------------------------------------------------------
void cc_htmlDecode(char* s) {
    if (!s) return;
    char* out = s;
    char* p = s;
    while (*p) {
        if (*p == '&') {
            struct { const char* n; char c; } T[] = {
                {"&ldquo;",'"'},{"&rdquo;",'"'},{"&lsquo;",'\''},{"&rsquo;",'\''},
                {"&nbsp;",' '},{"&mdash;",'-'},{"&ndash;",'-'},{"&amp;",'&'},
                {"&quot;",'"'},{"&#39;",'\''},{"&#8217;",'\''},{"&#8216;",'\''},
                {"&#8220;",'"'},{"&#8221;",'"'},{"&#8212;",'-'},{"&#8211;",'-'},
                {"&#160;",' '},{"&lt;",'<'},{"&gt;",'>'},
            };
            bool handled = false;
            for (size_t i = 0; i < sizeof(T)/sizeof(T[0]); i++) {
                size_t tl = strlen(T[i].n);
                if (strncmp(p, T[i].n, tl) == 0) {
                    *out++ = T[i].c;
                    p += tl;
                    handled = true;
                    break;
                }
            }
            if (handled) continue;
            // numeric decimal &#DDD;
            if (p[1] == '#') {
                char* e;
                long v = strtol(p + 2, &e, 10);
                if (e && *e == ';') {
                    if      (v == 8220 || v == 8221) *out++ = '"';
                    else if (v == 8216 || v == 8217) *out++ = '\'';
                    else if (v == 8211 || v == 8212) *out++ = '-';
                    else if (v == 160)               *out++ = ' ';
                    else                             *out++ = '?';
                    p = e + 1;
                    continue;
                }
            }
        }
        *out++ = *p++;
    }
    *out = '\0';
}

// Strip a leading bracketed section heading like "[Final Exhortations] " that
// sometimes prefixes the VotD text. The heading may sit INSIDE the verse's
// opening quote —  `"[Final Exhortations]  Rejoice..."` — so step past one
// opening quote (which is part of the verse and stays) and remove only the
// bracketed heading that follows it. Idempotent; shared by host and device.
void cc_stripLeadingBracket(char* s) {
    char* p = s;
    if (*p == '"' || *p == '\'') p++;        // opening quote: keep it, look past it
    if (*p == '[') {
        char* close = strchr(p, ']');
        if (close) {
            char* rest = close + 1;
            while (*rest == ' ') rest++;
            memmove(p, rest, strlen(rest) + 1);   // overwrite the bracket only
        }
    }
}

// ---------------------------------------------------------------------------
// HOST-ONLY: JSON extractor + fetch (curl hook). The device parses with
// ArduinoJson in net_impl_esp32.cpp; it never includes this section.
// ---------------------------------------------------------------------------
#ifdef CHROMAWOTD_HOST

static const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char* parseJsonString(const char* p, char* out, size_t outsz) {
    p = skipWs(p);
    if (*p != '"') return nullptr;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        if (n + 5 >= outsz) return nullptr;
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"' : out[n++] = '"';  p++; break;
                case '\'': out[n++] = '\''; p++; break;
                case '\\': out[n++] = '\\'; p++; break;
                case '/' : out[n++] = '/';  p++; break;
                case 'b' : out[n++] = '\b'; p++; break;
                case 'f' : out[n++] = '\f'; p++; break;
                case 'n' : out[n++] = '\n'; p++; break;
                case 'r' : out[n++] = '\r'; p++; break;
                case 't' : out[n++] = '\t'; p++; break;
                case 'u': {
                    p++;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9')      cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return nullptr;
                    }
                    if (n + 4 >= outsz) return nullptr;
                    if (cp < 0x80)                          out[n++] = (char)cp;
                    else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6));
                                           out[n++] = (char)(0x80 | (cp & 0x3F)); }
                    else {                out[n++] = (char)(0xE0 | (cp >> 12));
                                           out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                           out[n++] = (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: return nullptr;
            }
        } else {
            out[n++] = *p++;
        }
    }
    if (*p != '"') return nullptr;
    out[n] = '\0';
    return p + 1;
}

static const char* parseJsonNumber(const char* p, char* out, size_t outsz, double* val) {
    p = skipWs(p);
    size_t n = 0;
    if (*p == '-') { out[n++] = *p++; }
    while (*p >= '0' && *p <= '9') { if (n + 1 >= outsz) return nullptr; out[n++] = *p++; }
    if (*p == '.') { if (n + 1 >= outsz) return nullptr; out[n++] = *p++;
                     while (*p >= '0' && *p <= '9') { if (n + 1 >= outsz) return nullptr; out[n++] = *p++; } }
    out[n] = '\0';
    if (n == 0 || (n == 1 && out[0] == '-')) return nullptr;
    *val = atof(out);
    return p;
}

// Find the first JSON object member named `key` whose value parses as the
// requested type. Mismatched-type values (e.g. the string copies inside the
// *_units objects) are skipped and scanning continues, so a caller asking for
// a number lands on the numeric current member, not the string _units one.
static bool parseMember(const char* buf, const char* key,
                        bool wantString, char* out, size_t outsz, double* outVal) {
    const size_t klen = strlen(key);
    const char* p = buf;
    int iter = 0;
    while ((p = strstr(p, key)) != nullptr && iter++ < 20) {
        // A JSON member is `"key":` — the key text is wrapped in quotes, so after
        // the matched key string must come a closing quote then ':'.
        bool open = (p == buf) ? true : (p[-1] == '"');
        if (open && p[klen] == '"') {
            const char* q = skipWs(p + klen + 1);
            if (*q == ':') {
                const char* vp = skipWs(q + 1);
                if (wantString) {
                    if (parseJsonString(vp, out, outsz)) return true;
                } else {
                    char nb[24];
                    if (parseJsonNumber(vp, nb, sizeof(nb), outVal)) return true;
                }
                p = q + 1;
                continue;
            }
        }
        p += klen;
    }
    return false;
}

static WeatherIcon iconFromWmo(int code) {
    if (code == 0)                return WeatherIcon::Sun;
    if (code >= 1 && code <= 3)   return WeatherIcon::PartlyCloudy;
    if (code >= 80 && code <= 99) return WeatherIcon::Rain;
    if (code >= 45)               return WeatherIcon::Cloud;
    return WeatherIcon::PartlyCloudy;
}

bool cc_fetchWeather(WeatherData* out) {
    if (!out) return false;
    char url[520];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f"
        "&current=temperature_2m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=%s&forecast_days=1",
        CHROMAWOTD_LATITUDE, CHROMAWOTD_LONGITUDE, CHROMAWOTD_TIMEZONE);
    char json[4096];
    if (!cc_fetchJson(url, json, sizeof(json))) return false;

    double temp = 0.0, wmo = -1.0;
    if (!parseMember(json, "temperature_2m", false, nullptr, 0, &temp)) return false;
    if (!parseMember(json, "weather_code",  false, nullptr, 0, &wmo))   return false;
    if (wmo < 0.0) return false;

    static char cond[NET_TEXT_MAX];
    static char alert[NET_TEXT_MAX];
    bool needAlert = false;
    int wmu = (int)wmo;
    cc_wmoCondition(wmu, cond, sizeof(cond), &needAlert);
    if (needAlert) {
        cc_alertFromWmo(wmu, alert, sizeof(alert));
        out->alert = alert;
    } else {
        out->alert = nullptr;
    }
    out->temp      = (float)temp;
    out->condition = cond;
    out->icon      = iconFromWmo(wmu);
    return true;
}

bool cc_fetchVerse(VerseData* out) {
    if (!out) return false;
    char json[4096];
    if (!cc_fetchJson("https://www.biblegateway.com/votd/get/?format=json&version=NIV",
                      json, sizeof(json))) return false;

    static char text[NET_TEXT_MAX];
    static char ref[NET_TEXT_MAX];
    static char date[NET_TEXT_MAX];

    char ym[8] = "", dm[8] = "";
    if (!parseMember(json, "text",      true, text, sizeof(text), nullptr)) return false;
    if (!parseMember(json, "reference", true, ref,  sizeof(ref),  nullptr)) return false;
    if (!parseMember(json, "year",      true, date, sizeof(date), nullptr) ||
        !parseMember(json, "month",     true, ym,   sizeof(ym),   nullptr) ||
        !parseMember(json, "day",       true, dm,   sizeof(dm),   nullptr)) return false;

    cc_htmlDecode(text);
    cc_stripLeadingBracket(text);

    snprintf(date, sizeof(date), "%s-%s-%s", date, ym, dm);

    out->verse     = text;
    out->highlight = nullptr;
    out->reference = ref;
    out->date      = date;
    return true;
}

#endif  // CHROMAWOTD_HOST