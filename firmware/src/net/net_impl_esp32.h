// net_impl_esp32.h — device-only network entry points for main.cpp.
//
// Declares the functions net_impl_esp32.cpp implements. This header is
// TRANSLATION-UNIT-ONLY: it is included only from main.cpp (never from net.cpp
// or the host build) so the device's <Arduino.h>/<WiFi.h> include graph never
// leaks into the shared/host compile.

#pragma once

#include "net/net.h"

// Connect to Wi-Fi (from secrets.h). Returns 0 on success, non-zero on failure.
// Bounded ~15s timeout; never logs the passphrase.
int cc_wifiConnect();

// Fetch + parse into the given structs. These override the (host-only) ones in
// net.cpp — the device build does not compile net.cpp's #ifdef block.
bool cc_fetchWeather(WeatherData* out);
bool cc_fetchVerse(VerseData* out);