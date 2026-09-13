// config.cpp — pure configuration logic (no flash access).
//
// Everything here runs on BOTH the device and the host test harness: defaults,
// validation, coordinate parsing and bounded copying. NVS/Preferences access lives
// in config_nvs.cpp so this unit stays testable without hardware.
#include "config/config.h"

#include "config/config_compiletime.h"

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
    // Values are strings on the right-hand side: cc_configCopy keeps them bounded.
    // WIFI_SSID may legitimately be "" (fresh clone), which marks the device
    // unprovisioned and is exactly what gates the setup portal.
    cc_configCopy(cfg->ssid, sizeof(cfg->ssid), WIFI_SSID);
    cc_configCopy(cfg->passphrase, sizeof(cfg->passphrase), WIFI_PASSPHRASE);
    cc_configCopy(cfg->timezone, sizeof(cfg->timezone), CHROMAWOTD_TIMEZONE);
    cc_configCopy(cfg->hostname, sizeof(cfg->hostname), "");
    cfg->latitude = CHROMAWOTD_LATITUDE;
    cfg->longitude = CHROMAWOTD_LONGITUDE;
    cfg->contentMode = 0; // 0 = time-based (the documented default)
    cc_configCopy(cfg->configuredBy, sizeof(cfg->configuredBy), "");
}

bool cc_configIsProvisioned(const DeviceConfig& cfg) {
    return cfg.ssid[0] != '\0';
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
        reason = "Timezone is required (POSIX form, e.g. AEST-10AEDT,M10.1.0,M4.1.0/3).";
    } else if (strnlen(cfg.timezone, CC_CFG_TZ_MAX) >= CC_CFG_TZ_MAX) {
        reason = "Timezone string is too long.";
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
