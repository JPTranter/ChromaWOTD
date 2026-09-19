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

// Our configuration lives in its OWN NVS partition ("nvs_cfg", declared in
// firmware/partitions.csv) rather than in the default "nvs".
//
// The Arduino core's initArduino() erases the whole of the FIRST data/nvs partition
// whenever nvs_flash_init() reports ESP_ERR_NVS_NO_FREE_PAGES
// (cores/esp32/esp32-hal-misc.c). The default partition is only 20 KB and is shared
// with the WiFi/BLE/DHCP stack, so when WiFi state filled it, our stored credentials
// were destroyed along with it and the device silently re-offered its setup portal
// (BUG-06 / LESSONS §45). A dedicated partition is out of that erase's reach.
//
// Preferences::begin() calls nvs_flash_init_partition(label) itself, so passing this
// label is the entire change — no separate init call is required.
#ifdef CHROMAWOTD_LEGACY_NVS
// PRE-FIX behaviour, for the A/B demonstration only (env:nvsprobe_legacy): keep the
// configuration in the SHARED default "nvs" partition — the layout BUG-06 destroyed.
// This flag is required for the "before" case to exist at all: without it the legacy
// build would still ask for `nvs_cfg`, a partition its table does not define, so
// Preferences::begin() would fail, the setup portal could never save, and there would be
// no stored configuration to lose. That would demonstrate nothing.
constexpr const char* kPartition = nullptr; // nullptr => the default ("nvs") partition
#else
constexpr const char* kPartition = "nvs_cfg";
#endif
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

// Store a string key, or REMOVE it when the value is empty. A helper rather than a
// putBytes() call per site, for two reasons:
//   * Preferences::putBytes() returns size_t, so the `>= 0` test it used to carry was
//     ALWAYS true — a failed write passed silently. Comparing the returned length
//     against the length requested is the only real check.
//   * An empty value must DELETE the key, not write a zero-length blob: otherwise a
//     cleared passphrase (open network) or hostname leaves the previous value in NVS,
//     and readStr() would overlay it as though it were current.
bool putStr(Preferences& p, const char* key, const char* value, size_t cap) {
    const size_t n = strnlen(value, cap);
    if (n == 0) {
        p.remove(key); // an absent key is fine; nothing to clear
        return true;
    }
    // Length WITHOUT a trailing NUL; readStr() adds it back on the way out.
    return p.putBytes(key, value, n) == n;
}

// Read the configuration back from flash and compare it against what was just written.
//
// This is the read-back check BUG-06's plan called for: a silently-failed NVS write was
// indistinguishable from a successful one, because the per-key return codes could not
// express failure (see putStr above) and nothing ever verified the result. The portal now
// refuses to report "saved" unless the values are really in flash.
bool verifyStored(const DeviceConfig& want) {
    DeviceConfig got{};
    cc_configDefaults(&got); // start from defaults, then overlay what is actually stored
    if (!cc_configLoadFromNvs(&got))
        return false; // nothing came back at all
    return strncmp(want.ssid, got.ssid, sizeof(want.ssid)) == 0 &&
           strncmp(want.passphrase, got.passphrase, sizeof(want.passphrase)) == 0 &&
           strncmp(want.timezone, got.timezone, sizeof(want.timezone)) == 0 &&
           strncmp(want.hostname, got.hostname, sizeof(want.hostname)) == 0 &&
           strncmp(want.configuredBy, got.configuredBy, sizeof(want.configuredBy)) == 0 &&
           want.latitude == got.latitude && want.longitude == got.longitude &&
           want.contentMode == got.contentMode;
}
} // namespace

bool cc_configLoadFromNvs(DeviceConfig* cfg) {
    if (!cfg)
        return false;
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/true, kPartition))
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
    if (!p.begin(kNamespace, /*readOnly=*/false, kPartition))
        return false;

    // putBytes with an explicit length writes the string WITHOUT a trailing NUL;
    // readStr() adds it back. This avoids storing a length that includes padding.
    // putStr() also deletes a key whose new value is empty (see its comment).
    bool ok = true;
    ok &= putStr(p, kKeySsid, cfg.ssid, sizeof(cfg.ssid));
    ok &= putStr(p, kKeyPass, cfg.passphrase, sizeof(cfg.passphrase));
    ok &= putStr(p, kKeyTz, cfg.timezone, sizeof(cfg.timezone));
    ok &= putStr(p, kKeyHost, cfg.hostname, sizeof(cfg.hostname));
    ok &= putStr(p, kKeyVer, cfg.configuredBy, sizeof(cfg.configuredBy));
    ok &= p.putFloat(kKeyLat, cfg.latitude) > 0;
    ok &= p.putFloat(kKeyLon, cfg.longitude) > 0;
    ok &= p.putUChar(kKeyMode, cfg.contentMode) > 0;
    ok &= p.putUChar(kKeyMagic, 1) > 0;

    p.end();
    // Verify by reading it all back: return codes alone cannot express a silent NVS write
    // failure, and the portal must not report "saved" unless the values are really there.
    return ok && verifyStored(cfg);
}

bool cc_configEraseNvs() {
    Preferences p;
    if (!p.begin(kNamespace, /*readOnly=*/false, kPartition))
        return false;
    const bool ok = p.clear();
    p.end();
    return ok;
}

#endif // !CHROMAWOTD_HOST
