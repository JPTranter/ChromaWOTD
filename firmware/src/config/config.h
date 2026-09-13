// config.h — runtime device configuration (NVS-backed, with compile-time fallback).
//
// Resolution order, highest priority first:
//   1. NVS        — what the setup portal wrote (survives reflashing the app-only
//                   image, because NVS lives at 0x9000 and an app-only write starts
//                   at 0x10000).
//   2. built-in   — location/timezone defaults so a device still behaves coherently;
//                   there is NO compile-time credential path.
//   3. Built-in   — so a fresh clone still builds and boots.
//
// This is what makes "update the app but keep my settings" work: nothing here is
// baked into the image, so replacing app0 cannot lose configured values.
//
// The load/save pair is pure logic over a struct so it can be unit-tested; flash
// access (Preferences) is confined to config_nvs.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// Longest strings accepted from the portal. Deliberately generous for SSIDs (32
// chars max per 802.11, plus a NUL) but bounded everywhere, because these arrive
// from an untrusted HTTP form.
static constexpr size_t CC_CFG_SSID_MAX = 33;
static constexpr size_t CC_CFG_PASS_MAX = 65; // 63-char WPA2 passphrase + NUL
static constexpr size_t CC_CFG_TZ_MAX = 48;   // POSIX TZ strings are short
static constexpr size_t CC_CFG_HOST_MAX = 33;
static constexpr size_t CC_CFG_VERSION_MAX = 24; // recorded firmware version

struct DeviceConfig {
    char ssid[CC_CFG_SSID_MAX];
    char passphrase[CC_CFG_PASS_MAX];
    char timezone[CC_CFG_TZ_MAX];
    float latitude;
    float longitude;
    char hostname[CC_CFG_HOST_MAX];        // "" = use the compile-time default
    uint8_t contentMode;                   // 0 = time-based (default), 1 = force verse, 2 = force word
    char configuredBy[CC_CFG_VERSION_MAX]; // firmware version that wrote this
};

// Fill `cfg` with the compile-time/built-in values (no NVS access). Always
// succeeds; call this before loadFromNvs so the NVS values can overlay it.
void cc_configDefaults(DeviceConfig* cfg);

// Overlay any values present in NVS onto `cfg`. Returns true when at least one
// NVS value was applied (i.e. the device has been provisioned). Does not clear
// fields with no stored value, so the fallbacks survive.
bool cc_configLoadFromNvs(DeviceConfig* cfg);

// True when the configuration is usable for a network connection: a non-empty
// SSID. This is the "is the device provisioned?" test that gates the setup portal.
bool cc_configIsProvisioned(const DeviceConfig& cfg);

// Validate + clamp a candidate configuration (used by the portal submit handler).
// Returns true when the values are acceptable; on failure `err` receives a short
// reason suitable for showing back to the user. Enforces:
//   - ssid non-empty and length < CC_CFG_SSID_MAX
//   - passphrase length < CC_CFG_PASS_MAX (empty allowed for open networks)
//   - timezone non-empty and length < CC_CFG_TZ_MAX
//   - latitude in [-90, 90], longitude in [-180, 180], and NOT both zero
//     (0,0 is the classic "unset" value and would put the forecast in the Atlantic)
//   - contentMode <= 2
bool cc_configValidate(const DeviceConfig& cfg, char* err, size_t errsz);

// Persist the whole struct to NVS. Returns false on a storage failure.
bool cc_configSaveToNvs(const DeviceConfig& cfg);

// Erase the configuration namespace (factory reset). Returns false on failure.
bool cc_configEraseNvs();

// --- Pure helpers (unit-tested without flash) -------------------------------
// Parse a decimal latitude/longitude string. Returns false when the text is not a
// valid finite number in range. Accepts a leading '-' and an optional decimal part.
bool cc_configParseCoord(const char* text, float* out, float min, float max);

// Copy a bounded, NUL-terminated string into `dst` (always terminated). Returns
// the number of characters copied (excluding the NUL).
size_t cc_configCopy(char* dst, size_t dstsz, const char* src);
