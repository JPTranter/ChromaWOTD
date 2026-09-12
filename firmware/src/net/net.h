// net.h — Network data fetching + parsing + mapping for CHROMAWOTD (Phase 3).
//
// Surface shared by BOTH the host harness and the device:
//   - the VerseData / WeatherData structs the layout engine renders
//   - parse + mapping functions pushed down into this TU (pure, unit-testable)
//   - a JSON-fetch hook whose implementation is picked by compile target:
//        CHROMAWOTD_HOST   -> curl subprocess (tools/ or harness)
//        device            -> WiFiClientSecure + HTTPClient (real TLS)
//
// The layout engine only ever sees the structs; this module is where network
// bytes become VerseData/WeatherData. Everything below is pure C structs and
// <cstring>/<cmath> so it compiles in both worlds (the device's WiFi/TLS
// includes live only in net_impl_esp32.cpp, never in this header).
//
// TYPE LIMITS (the concrete contract that makes host/device agree):
//   - Verses, references, conditions and alerts are TRUNCATED to NET_TEXT_MAX
//     (230) bytes in cc_fetchVerse / cc_fetchWeather *before* any draw call.
//     The renderer's own bounds (wordBuf[64], lineBuf[96]) are a SECOND, tighter
//     boundary on top of this; nothing network-originated is ever sprintf'd.
//   - Temperatures / WMO codes are floats/ints; there is no numeric string
//     parsing on the device.
//   - All returned strings point into caller-provided buffers (never static
//     reusable state), so two consecutive fetches cannot clobber each other.

#pragma once
#include <cstddef>
#include "verse_display.h"   // VerseData, WeatherData, WeatherIcon

#if defined(CHROMAWOTD_HOST)
extern bool cc_fetchJson(const char* url, char* out, size_t outsz);
#else
extern bool cc_fetchJsonThrottled(const char* url, char* out, size_t outsz);
#endif

static constexpr int NET_TEXT_MAX = 230;   // char-buffer cap for fetched text/condition

// --- Weather (Open-Meteo) ---
struct ProofWeather {
    float temp;
    const char* condition;   // may be null (no human text from Open-Meteo)
};

// Map a WMO `weather_code` to a bounded condition string in `buf`.
// Fills `*outNeedAlert` when the code maps to a severe-weather alert. Returns
// chars written (excluding NUL), or -1 if buf was too small.
int cc_wmoCondition(int code, char* buf, size_t bufsz, bool* outNeedAlert);

// Fetch + parse Open-Meteo for the configured location into a WeatherData.
// On success returns true and fills *out (alert may be null). On any failure
// returns false and leaves *out untouched.
bool cc_fetchWeather(WeatherData* out);

// --- Verse (BibleGateway) ---
// Decode common HTML entities in place in `s` (safe: every sequence maps to a
// shorter-or-equal ASCII string, so the buffer never overflows). Handles the
// quoted brackets that wrap some VotD titles and `&amp;` in the reference.
void cc_htmlDecode(char* s);

// Remove a leading bracketed section heading (e.g. "[Final Exhortations] ")
// from VotD text, keeping the verse's own opening/closing quotes intact.
// The heading may sit inside the opening quote; only the bracket goes.
void cc_stripLeadingBracket(char* s);

// Fetch + parse BibleGateway VotD into a VerseData. On success returns true and
// fills *out (highlight chosen per verse text; reference + date filled). On
// failure returns false and leaves *out untouched.
bool cc_fetchVerse(VerseData* out);

// --- Alerts ---
// Map a WMO code onto an alert string in `buf` (or empty string = no alert).
int cc_alertFromWmo(int wmoCode, char* buf, size_t bufsz);