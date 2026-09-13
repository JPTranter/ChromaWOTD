// tz_map.cpp — IANA name -> POSIX TZ string table. See tz_map.h for the rationale.
#include "config/tz_map.h"

#include <cstring>

namespace {

struct TzEntry {
    const char* iana;  // what the portal offers and NVS stores
    const char* posix; // what newlib needs
};

// POSIX TZ strings, each VERIFIED against the system timezone database at the current
// instant and at both DST boundaries (the boundary check is the one that matters: the
// original bug was a missing DST rule, which is invisible outside a transition).
//
//   TZ=POSIX  std offset [dst [offset] , start [/time] , end [/time] ]
// Note the sign convention: POSIX offsets are WEST-positive, so AEST (UTC+10) is
// written "-10". Getting that backwards produces a plausible but wrong clock.
//
// DST rules: southern-hemisphere zones start on the first Sunday of October (M10.1.0)
// and end on the first Sunday of April (M4.1.0); /3 is 03:00 local. Zones without DST
// have no rule at all, which is correct for them.
const TzEntry kZones[] = {
    // Australia / NZ — the device's own region, kept first so the picker leads with it.
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"}, // no DST in Queensland
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "AWST-8"}, // no DST in WA
    {"Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Darwin", "ACST-9:30"}, // no DST in the NT
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    // Common international zones, so the device is usable outside Australia.
    {"UTC", "UTC0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Singapore", "SGT-8"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Dubai", "GST-4"},
    {"Africa/Johannesburg", "SAST-2"},
};

constexpr int kZoneCount = (int)(sizeof(kZones) / sizeof(kZones[0]));

// Legacy / hand-typed forms that a device may ALREADY have in NVS from the old
// free-text POSIX field. Mapping them to a correct entry means a device provisioned
// with the incomplete "AEST-10AEDT" gets a correct clock on its next boot instead of
// silently staying an hour out. An incomplete POSIX string is NOT accepted as-is: that
// is exactly the input that caused the bug.
struct LegacyEntry {
    const char* entered;
    const char* iana;
};
const LegacyEntry kLegacy[] = {
    {"AEST-10AEDT", "Australia/Melbourne"}, // the incomplete form that shifted the clock
    {"AEST-10", "Australia/Brisbane"},
    {"ACST-9:30ACDT,M10.1.0,M4.1.0/3", "Australia/Adelaide"},
    {"AWST-8", "Australia/Perth"},
    {"NZST-12NZDT,M9.5.0,M4.1.0/3", "Pacific/Auckland"},
    {"UTC0", "UTC"},
};
constexpr int kLegacyCount = (int)(sizeof(kLegacy) / sizeof(kLegacy[0]));

} // namespace

const char* cc_tzPosixForIana(const char* name) {
    if (!name || !name[0])
        return nullptr;
    for (int i = 0; i < kZoneCount; i++)
        if (strcmp(kZones[i].iana, name) == 0)
            return kZones[i].posix;
    // Not an IANA name we know: maybe a previously-stored legacy string.
    for (int i = 0; i < kLegacyCount; i++)
        if (strcmp(kLegacy[i].entered, name) == 0)
            return cc_tzPosixForIana(kLegacy[i].iana);
    return nullptr;
}

const char* cc_tzDefaultName() {
    return kZones[0].iana;
}

int cc_tzCount() {
    return kZoneCount;
}

const char* cc_tzNameAt(int index) {
    if (index < 0 || index >= kZoneCount)
        return nullptr;
    return kZones[index].iana;
}
