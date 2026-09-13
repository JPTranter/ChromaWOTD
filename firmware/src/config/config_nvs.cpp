// config_nvs.cpp — NVS (Preferences) persistence for DeviceConfig.
//
// Device-only: compiled when !CHROMAWOTD_HOST. Kept apart from config.cpp so the
// validation/parsing logic stays testable on the host without flash.
//
// Layout: one Preferences namespace ("chromawotd") with a key per field, so a
// partial write cannot corrupt unrelated values and a future field can be added
// without invalidating existing storage. An empty string means "not set", which
// lets cc_configLoadFromNvs() overlay only what the portal actually wrote and
// leave the compile-time fallbacks in place for the rest.
//
// SECURITY: values here are read from the network (portal form) and written to
// flash unencrypted. NVS is not a secure store — it is protected by the device's
// physical possession, nothing more. The passphrase is never logged.
#include "config/config.h"

#ifndef CHROMAWOTD_HOST

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "chromawotd";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";
constexpr const char* kKeyTz = "tz";
constexpr const char* kKeyLat = "lat";
constexpr const char* kKeyLon = "lon";
constexpr const char* kKeyHost = "host";
constexpr const char* kKeyMode = "mode";
constexpr const char* kKeyVer = "cfgver";
constexpr const char* kKeyMagic = "cfgok"; // 1 == provisioned by the portal

// Read a string key into a bounded buffer; leaves `dst` untouched when the key is
// absent, so the caller's fallback survives.
bool readStr(Preferences& p, const char* key, char* dst, size_t dstsz, bool* any) {
    const size_t n = p.getBytesLength(key);
    if (n == 0)
        return false;
    char tmp[CC_CFG_PASS_MAX > CC_CFG_TZ_MAX ? CC_CFG_PASS_MAX : CC_CFG_TZ_MAX];
    if (n >= sizeof(tmp))
        return false; // implausible size: ignore
    if (p.getBytes(key, tmp, n) != n)
        return false;
    tmp[n] = '\0';
    cc_configCopy(dst, dstsz, tmp);
    *any = true;
    return true;
}
} // namespace

bool cc_configLoadFromNvs(DeviceConfig* cfg) {
    if (!cfg)
        return false;
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/true))
        return false;

    bool any = false;
    // An empty SSID in NVS must NOT be treated as "provisioned" — the portal only
    // writes a validated (non-empty) SSID, so this is a defensive check.
    readStr(p, kKeySsid, cfg->ssid, sizeof(cfg->ssid), &any);
    readStr(p, kKeyPass, cfg->passphrase, sizeof(cfg->passphrase), &any);
    readStr(p, kKeyTz, cfg->timezone, sizeof(cfg->timezone), &any);
    readStr(p, kKeyHost, cfg->hostname, sizeof(cfg->hostname), &any);
    readStr(p, kKeyVer, cfg->configuredBy, sizeof(cfg->configuredBy), &any);

    if (p.isKey(kKeyLat)) {
        cfg->latitude = p.getFloat(kKeyLat, cfg->latitude);
        any = true;
    }
    if (p.isKey(kKeyLon)) {
        cfg->longitude = p.getFloat(kKeyLon, cfg->longitude);
        any = true;
    }
    if (p.isKey(kKeyMode)) {
        cfg->contentMode = p.getUChar(kKeyMode, cfg->contentMode);
        any = true;
    }

    p.end();
    return any;
}

bool cc_configSaveToNvs(const DeviceConfig& cfg) {
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/false))
        return false;

    // putBytes with an explicit length writes the string WITHOUT a trailing NUL;
    // readStr() adds it back. This avoids storing a length that includes padding.
    bool ok = true;
    ok &= p.putBytes(kKeySsid, cfg.ssid, strnlen(cfg.ssid, sizeof(cfg.ssid))) > 0;
    ok &= p.putBytes(kKeyPass, cfg.passphrase, strnlen(cfg.passphrase, sizeof(cfg.passphrase))) >= 0;
    ok &= p.putBytes(kKeyTz, cfg.timezone, strnlen(cfg.timezone, sizeof(cfg.timezone))) > 0;
    ok &= p.putBytes(kKeyHost, cfg.hostname, strnlen(cfg.hostname, sizeof(cfg.hostname))) >= 0;
    ok &= p.putBytes(kKeyVer, cfg.configuredBy, strnlen(cfg.configuredBy, sizeof(cfg.configuredBy))) >= 0;
    ok &= p.putFloat(kKeyLat, cfg.latitude) > 0;
    ok &= p.putFloat(kKeyLon, cfg.longitude) > 0;
    ok &= p.putUChar(kKeyMode, cfg.contentMode) > 0;
    ok &= p.putUChar(kKeyMagic, 1) > 0;

    p.end();
    return ok;
}

bool cc_configEraseNvs() {
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/false))
        return false;
    const bool ok = p.clear();
    p.end();
    return ok;
}

#endif // !CHROMAWOTD_HOST
