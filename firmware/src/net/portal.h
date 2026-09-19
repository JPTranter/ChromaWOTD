// portal.h — first-boot captive portal for Wi-Fi/location configuration.
//
// Flow (matches docs/ARCHITECTURE.md "FirstBoot/Setup"):
//   1. The device has no usable config (empty SSID in NVS and compile-time).
//   2. It raises a SoftAP secured with a PER-BOOT RANDOM password and shows the AP
//      name + password on the ePaper, because a secured AP with nobody able to read
//      the password is a lockout. The panel is the only trustworthy channel: it is
//      physical, and the portal is deliberately reachable by anyone in radio range.
//   3. DNSServer answers every DNS query with the device IP, so phones/laptops pop
//      their "sign in to network" sheet (captive portal detection).
//   4. The form posts SSID / passphrase / timezone / lat / lon, which are validated
//      and saved to NVS, then the device reboots into its normal sync path.
//
// The AP password is regenerated on every boot and is never persisted, so a
// captured password is useless after the device leaves setup mode.
#pragma once

#include <cstddef>
#include <cstdint>

#include "config/config.h"

// AP name is derived from the chip MAC so two devices on a bench are distinguishable.
static constexpr size_t CC_PORTAL_APNAME_MAX = 32;
// WPA2 requires a passphrase of AT LEAST 8 characters — the ESP32 core rejects a
// shorter one outright (WiFiAP.cpp: `strlen(passphrase) < 8`), so `WiFi.softAP()`
// would fail and the portal would never come up. 8 digits + NUL is the minimum
// viable length, kept numeric because the user reads it off a 4-colour panel.
static constexpr size_t CC_PORTAL_APASS_MAX = 9;
static constexpr size_t CC_PORTAL_APASS_LEN = 8;

// How long the setup portal stays up before giving up and going back to sleep.
// Bounded on purpose: this is a battery-powered device, so an unconfigured unit must
// not sit awake with an open SoftAP indefinitely. Long enough to find the panel,
// join the network and fill in a form (the panel refresh alone takes ~25 s).
static constexpr uint32_t CC_PORTAL_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 minutes

struct PortalInfo {
    char apName[CC_PORTAL_APNAME_MAX];
    char apPassword[CC_PORTAL_APASS_MAX];
};

// --- Wi-Fi network picker ---------------------------------------------------
// The form offers the networks the device can actually SEE, so the SSID is chosen
// rather than typed. A hand-typed, case-sensitive SSID is how a device ends up
// hunting a network that does not exist (LESSONS §50): after a partition change
// empties the stored config, the setup form has nothing to pre-fill and the user
// must retype the name from memory, on a phone, with the panel as the only display.
// Hidden networks are invisible to a scan, so a manual field stays alongside the list.
static constexpr size_t CC_PORTAL_SCAN_MAX = 12;      // options offered
static constexpr size_t CC_PORTAL_SCAN_SSID_MAX = 33; // 32-char SSID + NUL

struct PortalScanEntry {
    char ssid[CC_PORTAL_SCAN_SSID_MAX];
    int32_t rssi;
};

// What a repaint's status line MEANS, so the panel heading is accurate. Previously
// any status line was drawn under "Could not save:", which mislabels anything that is
// not a rejected save (e.g. reporting that previously-stored settings were lost).
enum class PortalNotice { None, SaveRejected, SettingsLost };

// Pure: render the <option> elements for a scan result into `out`. `current` is marked
// selected when it matches an entry. Escapes exactly like every other echoed value —
// an SSID may legally contain quotes or angle brackets, which would otherwise break
// out of `value='...'`. Host-tested; the device supplies the entries from a real scan.
// Returns the number of characters written.
size_t cc_portalRenderSsidOptions(const PortalScanEntry* entries, size_t count, const char* current,
                                  char* out, size_t outsz);

// Scan for visible networks and fill `out` (strongest signal first, duplicates
// removed). Returns the number stored. Device-only: needs the radio.
size_t cc_portalScanNetworks(PortalScanEntry* out, size_t maxOut);

// Derive the per-boot AP name and random WPA2 password. `seed` must be an
// unpredictable value (the device passes its hardware RNG); this function is pure
// so the *generation rule* is unit-testable — it never reads the RNG itself.
// Generates an 8-DIGIT password (the WPA2 minimum) because the user has to
// transcribe it from a 4-colour ePaper panel by eye, and digits are the easiest
// thing to read and type off a 6px font.
void cc_portalMakeInfo(uint32_t macLow, uint32_t seed, PortalInfo* out);

// Run the blocking setup portal: starts the SoftAP, serves the form, and returns
// only after a valid configuration has been saved (the caller should reboot), or
// after `timeoutMs` with no successful submit (returns false, caller may retry or
// give up into the offline path). Device-only.
bool cc_portalRun(const PortalInfo& info, uint32_t timeoutMs);

// Render the setup screen (AP name, password, URL, step list) on the panel.
// Implemented in verse_display.cpp, where the shared dev_drawString* helpers live,
// so the portal does not carry a second copy of the text/degree handling. A
// non-None `notice` repaints with `statusLine` under the matching heading instead of
// the setup steps.
void cc_portalDrawScreen(const PortalInfo& info, PortalNotice notice, const char* statusLine);
