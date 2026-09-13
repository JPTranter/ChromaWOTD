// config.cpp — pure configuration logic (no flash access).
//
// Everything here runs on BOTH the device and the host test harness: defaults,
// validation, coordinate parsing and bounded copying. NVS/Preferences access lives
// in config_nvs.cpp so this unit stays testable without hardware.
#include "config/config.h"

#include "config/config_compiletime.h"
#include "config/tz_map.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

size_t cc_configCopy(char* dst, size_t dstsz, const char* src) {
    if (!dst || dstsz == 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t i = 0;
    for (; src[i] && i + 1 < dstsz; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return i;
}

void cc_configDefaults(DeviceConfig* cfg) {
    if (!cfg)
        return;
    memset(cfg, 0, sizeof(*cfg));
    // The SSID is deliberately empty: credentials exist ONLY in NVS, written by the
    // setup portal. An empty SSID is what cc_configIsProvisioned() reports as
    // "unprovisioned", which sends a fresh device to the portal. There is no
    // compiled-in credential path any more (see config_compiletime.h).
    cfg->ssid[0] = '\0';
    cfg->passphrase[0] = '\0';
    // Store the timezone as an IANA NAME (the form the portal picker offers and
    // validation accepts). The POSIX string newlib needs is DERIVED from it at use
    // time via cc_configResolvedTz() -> cc_tzPosixForIana(), so the stored value and
    // the runtime value can never disagree about DST.
    cc_configCopy(cfg->timezone, sizeof(cfg->timezone), cc_tzDefaultName());
    cc_configCopy(cfg->hostname, sizeof(cfg->hostname), "");
    cfg->latitude = CHROMAWOTD_LATITUDE;
    cfg->longitude = CHROMAWOTD_LONGITUDE;
    cfg->contentMode = 0; // 0 = time-based (the documented default)
    cc_configCopy(cfg->configuredBy, sizeof(cfg->configuredBy), "");
}

bool cc_configIsProvisioned(const DeviceConfig& cfg) {
    return cfg.ssid[0] != '\0';
}

const char* cc_configResolvedTz(const DeviceConfig& cfg) {
    const char* posix = cc_tzPosixForIana(cfg.timezone);
    // Also accept a legacy POSIX string that is already correct, so a device
    // provisioned before the picker existed keeps working if its stored value happens
    // to be a complete rule. cc_tzPosixForIana() maps the known legacy forms; anything
    // else unknown yields nullptr and the caller falls back to the built-in default.
    return posix;
}

bool cc_configParseCoord(const char* text, float* out, float min, float max) {
    if (!text || !out)
        return false;
    // Skip leading whitespace; the portal may submit " -37.85".
    while (*text == ' ' || *text == '\t')
        text++;
    if (!*text)
        return false;
    if (!(*text == '-' || *text == '+' || (*text >= '0' && *text <= '9')))
        return false;

    char* end = nullptr;
    const double v = strtod(text, &end);
    if (end == text)
        return false;
    // Reject trailing garbage ("-37abc") rather than silently accepting the prefix.
    while (end && (*end == ' ' || *end == '\t'))
        end++;
    if (end && *end != '\0')
        return false;
    if (!std::isfinite(v))
        return false;
    if (v < min || v > max)
        return false;
    *out = (float)v;
    return true;
}

bool cc_configValidate(const DeviceConfig& cfg, char* err, size_t errsz) {
    const char* reason = nullptr;

    if (cfg.ssid[0] == '\0') {
        reason = "Wi-Fi network name is required.";
    } else if (strnlen(cfg.ssid, CC_CFG_SSID_MAX) >= CC_CFG_SSID_MAX) {
        reason = "Wi-Fi network name is too long.";
    } else if (strnlen(cfg.passphrase, CC_CFG_PASS_MAX) >= CC_CFG_PASS_MAX) {
        reason = "Wi-Fi password is too long (max 63 characters).";
    } else if (cfg.timezone[0] == '\0') {
        reason = "Timezone is required - pick your timezone from the list.";
    } else if (strnlen(cfg.timezone, CC_CFG_TZ_MAX) >= CC_CFG_TZ_MAX) {
        reason = "Timezone string is too long.";
    } else if (!cc_tzPosixForIana(cfg.timezone)) {
        // The value must be a known IANA name (or a legacy form we can map). A bare
        // POSIX string is rejected because an INCOMPLETE one is silently wrong: with no
        // DST rule newlib applies US DST dates, so "AEST-10AEDT" shifted the clock an
        // hour and scheduled the 06:30 wake for 05:30.
        reason = "Unrecognised timezone. Choose one from the list (e.g. Australia/Melbourne).";
    } else if (!(cfg.latitude >= -90.0f && cfg.latitude <= 90.0f)) {
        reason = "Latitude must be between -90 and 90.";
    } else if (!(cfg.longitude >= -180.0f && cfg.longitude <= 180.0f)) {
        reason = "Longitude must be between -180 and 180.";
    } else if (cfg.latitude == 0.0f && cfg.longitude == 0.0f) {
        // (0,0) is in the Atlantic off Africa. It is the classic "field left blank"
        // value, and silently accepting it would show a plausible-looking but wrong
        // forecast, so it is rejected rather than guessed at.
        reason = "Latitude and longitude cannot both be 0 - please enter your location.";
    } else if (cfg.contentMode > 2) {
        reason = "Content mode must be 0 (time-based), 1 (verse) or 2 (word).";
    }

    if (reason) {
        if (err && errsz)
            cc_configCopy(err, errsz, reason);
        return false;
    }
    if (err && errsz)
        err[0] = '\0';
    return true;
}
