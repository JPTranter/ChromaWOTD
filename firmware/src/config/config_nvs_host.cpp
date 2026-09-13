// config_nvs_host.cpp — host stub for the NVS persistence layer.
//
// config_nvs.cpp uses ESP32 Preferences and is compiled only for the device. The
// host test suite still links cc_configLoadFromNvs / cc_configSaveToNvs /
// cc_configEraseNvs (config_active.cpp calls the loader), so this file provides
// no-op implementations on the host: "nothing is stored" is the honest answer, and
// it keeps the pure configuration logic testable without pretending to have flash.
//
// Any host test that needs stored values should call cc_configSaveToNvs() and read
// them back through this in-memory store, so the round-trip contract is exercised
// even here.
#include "config/config.h"

#ifdef CHROMAWOTD_HOST

#include <cstring>

namespace {
// Small in-memory stand-in so a save/load round-trip is still meaningful on the
// host (the real device persists to flash; the semantics we verify are the same).
DeviceConfig g_stored;
bool g_haveStored = false;
} // namespace

bool cc_configLoadFromNvs(DeviceConfig* cfg) {
    if (!cfg || !g_haveStored)
        return false;
    *cfg = g_stored;
    return true;
}

bool cc_configSaveToNvs(const DeviceConfig& cfg) {
    g_stored = cfg;
    g_haveStored = true;
    return true;
}

bool cc_configEraseNvs() {
    memset(&g_stored, 0, sizeof(g_stored));
    g_haveStored = false;
    return true;
}

#endif // CHROMAWOTD_HOST
