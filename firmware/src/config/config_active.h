// config_active.h — the process-wide resolved device configuration.
//
// One instance, one resolution rule (NVS -> secrets.h -> built-in), consumed by
// both main.cpp and the network layer. Previously each translation unit re-derived
// its own defaults, and the copies had already drifted (Melbourne vs Sydney), so
// "host and device agree" was quietly false. Keeping the resolved value in one place
// is what makes that invariant real.
#pragma once

#include "config/config.h"

// The active configuration. Valid after cc_configInit(); before that it returns the
// built-in defaults so a caller that runs early still sees sane values.
const DeviceConfig& cc_configActive();

// Resolve the configuration once (defaults, then the NVS overlay) and return true
// when NVS supplied at least one value. Safe to call repeatedly: subsequent calls
// re-read NVS so a portal write is picked up without a reboot ordering dependency.
bool cc_configInit();

// Mutable access for the code paths that deliberately change settings at runtime
// (the portal saves through cc_configSaveToNvs and then reboots; this exists for
// tests and for a future in-place reload).
DeviceConfig* cc_configMutable();
