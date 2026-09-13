// config_compiletime.h — the single place that resolves COMPILE-TIME configuration.
//
// Resolution order (lowest priority first): the fallbacks below, overridden by
// secrets.h when it exists. secrets.h is gitignored and holds the developer's
// Wi-Fi credentials/location, so a fresh clone (and CI) has only
// secrets.h.example, whose values are commented-out placeholders — the build must
// therefore succeed with the fallbacks alone.
//
// This used to be duplicated in net_impl_esp32.cpp and net.cpp, and the two copies
// DISAGREED (Melbourne vs Sydney fallbacks), so the host harness and the device
// could compile different coordinates and the "host and device agree" invariant was
// quietly false. One definition now.
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("secrets.h.example")
#include "secrets.h.example"
#endif

// No credentials compiled in (fresh clone / CI). cc_wifiConnect() returns early
// when the SSID is empty, so these are never used to connect — they exist so the
// build succeeds. Runtime NVS configuration takes precedence at run time (see
// config.h).
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASSPHRASE ""
#endif

// Fallback location/timezone. Only used when neither NVS nor secrets.h supplies
// a value.
#ifndef CHROMAWOTD_LATITUDE
#define CHROMAWOTD_LATITUDE -37.8528
#endif
#ifndef CHROMAWOTD_LONGITUDE
#define CHROMAWOTD_LONGITUDE 145.1633
#endif
#ifndef CHROMAWOTD_TIMEZONE
#define CHROMAWOTD_TIMEZONE "Australia/Melbourne"
#endif

// DHCP hostname / mDNS name (overridable for a second device on one LAN).
#ifndef CHROMAWOTD_HOSTNAME
#define CHROMAWOTD_HOSTNAME "ChromaWOTD"
#endif
