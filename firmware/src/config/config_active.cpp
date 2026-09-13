// config_active.cpp — the shared resolved-configuration instance.
#include "config/config_active.h"

namespace {
DeviceConfig g_active;
bool g_loaded = false;

// Populate on first use with the built-in/compile-time values, so any caller that
// touches cc_configActive() before cc_configInit() still gets a coherent struct
// (all-zero would mean an empty SSID and an Atlantic coordinate).
void ensureLoaded() {
    if (!g_loaded) {
        cc_configDefaults(&g_active);
        g_loaded = true;
    }
}
} // namespace

const DeviceConfig& cc_configActive() {
    ensureLoaded();
    return g_active;
}

DeviceConfig* cc_configMutable() {
    ensureLoaded();
    return &g_active;
}

bool cc_configInit() {
    // Rebuild from defaults every time so a factory reset (which erases NVS) is not
    // masked by values loaded earlier in the same boot.
    cc_configDefaults(&g_active);
    g_loaded = true;
    return cc_configLoadFromNvs(&g_active);
}
