// tz_map.h — map an IANA timezone name to a POSIX TZ string.
//
// WHY THIS EXISTS: the portal used to ask for a POSIX TZ string directly, and an
// incomplete one fails silently. The device was configured with "AEST-10AEDT" (no DST
// rule), and newlib then applies US DST rules, shifting the clock +1 hour — the
// scheduler computed the 06:30 wake as 05:30 and the day/evening content boundary was
// wrong, with nothing in the UI to indicate a problem.
//
// An IANA name ("Australia/Melbourne") has no such partial form, so the portal asks
// for that and the POSIX string is DERIVED here.
//
// WHY A TABLE AND NOT tzdata: this framework is not built with the timezone database,
// so newlib cannot resolve an IANA name at runtime (`setenv("TZ", "Australia/Melbourne")`
// is only meaningful with tzdata present). A table keeps the mapping deterministic,
// dependency-free and unit-testable on the host.
#pragma once

#include <cstddef>

// Resolve `name` to a POSIX TZ string, or nullptr when it is not in the table.
// Matching is exact and case-sensitive (IANA names are), but a small set of LEGACY
// short forms is also accepted so a device already provisioned with one is corrected
// on its next boot rather than keeping a silently wrong clock — see the .cpp.
const char* cc_tzPosixForIana(const char* name);

// The name to show as the default option in the portal's picker.
const char* cc_tzDefaultName();

// Number of entries in the table, and the i-th IANA name — so the portal can render
// its <select> from the same source of truth the resolver uses (they cannot drift).
int cc_tzCount();
const char* cc_tzNameAt(int index);
