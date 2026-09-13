// config_compiletime.h — the built-in defaults for device configuration.
//
// There is deliberately NO compiled-in credential path. Wi-Fi credentials and
// location come from the device's own setup portal and live in NVS; nothing is ever
// read from a source file. The previous design supported a gitignored secrets.h that
// baked credentials into the firmware image, which meant a locally-built binary
// contained the developer's SSID and passphrase in plaintext — one such image was
// nearly published. Removing the mechanism removes that whole failure mode.
//
// Resolution order (highest priority first):
//   1. NVS          — what the setup portal wrote (survives an app-only reflash)
//   2. these values — so a device with no stored config still boots coherently
//
// A device with no NVS config has an empty SSID, which cc_configIsProvisioned()
// reports as unprovisioned and sends to the setup portal. The location/timezone
// defaults below are only a last resort, so a half-configured device still produces
// coherent output rather than an empty screen.
//
// This used to be duplicated in net_impl_esp32.cpp and net.cpp and the two copies
// DISAGREED (Melbourne vs Sydney), so host and device could compile different
// coordinates and the "host and device agree" invariant was quietly false.
#pragma once

// NOTE: there are deliberately no WIFI_SSID / WIFI_PASSPHRASE macros. Credentials
// exist only in the device's NVS, written by its setup portal — an empty SSID in
// cc_configDefaults() is what marks a device unprovisioned and sends it there.

// Location and timezone fallbacks (Burwood East, Victoria). Overridable at build
// time with -D if a particular build wants different defaults, but the normal path is
// the setup portal.
#ifndef CHROMAWOTD_LATITUDE
#define CHROMAWOTD_LATITUDE -37.8528
#endif
#ifndef CHROMAWOTD_LONGITUDE
#define CHROMAWOTD_LONGITUDE 145.1633
#endif
// NOTE: there is no CHROMAWOTD_TIMEZONE here. The timezone is stored as an IANA
// name (config/tz_map.cpp owns the name -> POSIX mapping), so a compile-time
// POSIX string would be a second, drift-prone source of truth.

// DHCP hostname / mDNS name (overridable for a second device on one LAN).
#ifndef CHROMAWOTD_HOSTNAME
#define CHROMAWOTD_HOSTNAME "ChromaWOTD"
#endif
