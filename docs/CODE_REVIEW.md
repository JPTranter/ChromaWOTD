# ChromaWOTD — Comprehensive Code & Architecture Review (Phases 2–5)

**Date:** 2026-09-18
**Scope:** `firmware/src/` (`main.cpp`, `config/`, `net/`, `sched/`, `draw/`, `text/`), `firmware/test/`, `tools/`, `docs/`, and CI workflows.
**Basis:** Full repository audit using CodeGraph, static analysis, unit test suite, and firmware build validation.

---

## Executive Summary

ChromaWOTD has made substantial progress since the Phase 1 layout bring-up. It now features an end-to-end operational stack on the Seeed XIAO ESP32-S3 + EE05 board: SoftAP setup wizard with Wi-Fi QR code rendering, NVS configuration persistence, hardware-verified multi-button deep sleep wake (`ext1`), time-slotted refreshes, automated font auto-sizing ladder (Roboto 6pt/5.5pt/5pt), and live HTTPS feeds for BibleGateway (Scripture), A.Word.A.Day (Vocabulary), and Open-Meteo (Weather).

However, our in-depth review identified **6 functional and UX bugs**, several **critical design/architectural risks** (including TLS certificate pinning issues and NVS partition vulnerability), and **extensive documentation drift** where status documents and architecture guides lag behind the implemented codebase.

---

## Priority Matrix of Findings

| ID | Category | Severity | Summary | Location |
|---|---|:---:|---|---|
| **BUG-01** | Bug | **P0** | Portal window timeout does not sleep; falls through and overwrites setup screen with OFFLINE error | [`firmware/src/main.cpp:376-384`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L376-L384) |
| **BUG-02** | Bug | **P1** | `contentMode` configuration saved to NVS but completely ignored by `main.cpp` | [`firmware/src/main.cpp:184`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L184) |
| **BUG-03** | Bug / UX | **P1** | Setup portal initial candidate coordinates are `(0, 0)`, failing validation on submit | [`firmware/src/net/portal.cpp:63, 254`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/portal.cpp#L63-L254) |
| **BUG-04** | Bug / Tool | **P2** | `test_portal_e2e.py` default timezone is POSIX string, immediately rejected by validation | [`tools/test_portal_e2e.py:91`](file:///C:/Users/jptra/Projects/ChromaWOTD/tools/test_portal_e2e.py#L91) |
| **BUG-05** | Bug / Logic | **P2** | Unsigned `putBytes(...) >= 0` check masks NVS write failures | [`firmware/src/config/config_nvs.cpp:93, 95, 96`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/config/config_nvs.cpp#L93-L96) |
| **BUG-06** | Architecture | **P1** | Silent wipe of stored config when shared 20 KB NVS partition fills | [`firmware/platformio.ini`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/platformio.ini), [`docs/lessons/LESSONS_LEARNT.md:1027`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/lessons/LESSONS_LEARNT.md#L1027) |
| **DES-01** | Design / TLS | **P1** | Pinned Let's Encrypt intermediate certificates (`YR2`, `YE1`) will break on renewal/rotation | [`firmware/src/net/net_impl_esp32.cpp:67-116`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/net_impl_esp32.cpp#L67-L116) |
| **DES-02** | Design / TLS | **P2** | Failed NTP time sync causes silent TLS verification failure on all HTTPS fetches | [`firmware/src/main.cpp:170-220`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L170-L220) |
| **DES-03** | Design / NVS | **P3** | Setting empty string in portal doesn't remove obsolete keys from NVS | [`firmware/src/config/config_nvs.cpp:84-104`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/config/config_nvs.cpp#L84-L104) |
| **INC-01** | Inconsistency | **P3** | Stale button wake comment references non-existent GPIO 5 | [`firmware/src/main.cpp:496`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L496) |
| **INC-02** | Inconsistency | **P3** | Duplicate `#ifndef CHROMAWOTD_HOSTNAME` macro definition | [`firmware/src/net/net_impl_esp32.cpp:289`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/net_impl_esp32.cpp#L289) |
| **INC-03** | Inconsistency | **P3** | Host `net.cpp` hardcodes coordinates instead of using `cc_configActive()` | [`firmware/src/net/net.cpp:549`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/net.cpp#L549) |
| **INC-04** | UX / Consistency | **P3** | Portal form wipes typed SSID if other fields fail validation | [`firmware/src/net/portal.cpp:121`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/portal.cpp#L121) |
| **INC-05** | Hygiene | **P3** | SoftAP radio not disconnected when portal closes | [`firmware/src/net/portal.cpp:312-315`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/portal.cpp#L312-L315) |
| **INC-06** | Tooling | **P3** | `bench_watch.py` hardcodes `DEFAULT_PORT = "COM13"` | [`tools/bench_watch.py:37`](file:///C:/Users/jptra/Projects/ChromaWOTD/tools/bench_watch.py#L37) |
| **DOC-01** | Documentation | **P2** | `docs/STATUS.md` header, next steps, and `main.cpp` claims are completely outdated | [`docs/STATUS.md:4, 44-67`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/STATUS.md#L4-L67) |
| **DOC-02** | Documentation | **P2** | `docs/ARCHITECTURE.md` asserts outdated button toggle mode and `secrets.h` | [`docs/ARCHITECTURE.md:3, 12, 78`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/ARCHITECTURE.md#L3-L78) |
| **DOC-03** | Documentation | **P3** | Broken relative link `[eClock](../eClock)` in README | [`README.md:7`](file:///C:/Users/jptra/Projects/ChromaWOTD/README.md#L7) |

---

## Fix status (2026-09-18, applied against this review)

Every finding was re-checked against the on-disk source before being acted on. All 24 were
real and the `file:line` citations were accurate. `python tools/verify_all.py` is green
after the fixes. Four items are re-graded or extended — see the Notes column.

| ID | Severity | Status | Notes |
|----|----------|--------|-------|
| BUG-01 | P0 → **P1** | **FIXED** | Re-graded. This review's own scale defines P0 as "must be fixed before shipping network code" — that code has already shipped, and the defect is a self-recovering UX regression (a button press re-opens the portal). The expired setup window now deep-sleeps through the same shared `armSleepAndSleep()` the normal cycle uses, so the setup screen survives and the next wake re-opens the portal. |
| BUG-02 | P1 | **FIXED** | `cc_resolveContentMode(configuredMode, haveTime, hour)` extracted into `sched/content_policy` (pure) and called from `main.cpp:184`. Four new host tests in `test_sched.cpp` lock the forced-mode behaviour. |
| BUG-03 | P1 | **FIXED** | `cc_portalRun()` seeds `g_candidate = cc_configActive()`, so the form pre-fills real defaults instead of `(0,0)` (and pre-selects the current timezone/mode). Dead local in `handleRoot()` removed. |
| BUG-04 | P2 | **FIXED** | `--tz` default is now `Australia/Melbourne`. Confirmed the old default really was rejected: `AEST-10AEDT,M10.1.0,M4.1.0/3` is neither an IANA name nor one of the legacy forms `tz_map.cpp` maps. |
| BUG-05 | P2 | **FIXED** | A `putStr()` helper compares `putBytes()`'s returned length against the length requested — the `>= 0` test on an unsigned return could never fail — and deletes the key when the value is empty. Fixed **together with DES-03** (same lines, one fix). |
| BUG-06 | P1 | **DEFERRED — deliberate** | Requires `firmware/partitions.csv` + `board_build.partitions` + `Preferences::begin("chromawotd", false, "nvs_cfg")`. That moves flash offsets, so it needs a FULL flash, hardware verification, and updates to the app-only-flash workflow and `tools/merge_firmware.py`. Shipping an unverified partition table that could brick the boot is worse than leaving a diagnosed bug in the backlog. |
| DES-01 | P1 | **FIXED — host- and hardware-verified** | YR2/YE1 *intermediates* replaced by a root bundle — ISRG Root X1 + ISRG Root X2 + Amazon Root CA 1 — applied as one `ROOT_CA_BUNDLE` for every host. Verified against all three LIVE chains with `openssl verify -CAfile <bundle> -untrusted <chain> <leaf>`. This also exposed a defect this review did not list (see below). **Confirmed on the device 2026-09-19**: the post-flash boot log shows both HTTPS fetches succeeding (`sync: verse OK`, `sync: weather OK`), i.e. real TLS validating against the new roots. |
| DES-02 | P2 | **FIXED** | The unset-clock case is now logged explicitly where it happens, and a failed fetch reports `PARTIAL: <api> failed (device clock not set)` instead of implying the servers are down. |
| DES-03 | P3 | **FIXED** | Implemented as one change with BUG-05. |
| INC-01 | P3 | **FIXED** | `main.cpp:496` corrected to GPIO2/3/8. The same stale text also sat in the **file header (`main.cpp:6`)**, which this review did not cite; both are fixed. |
| INC-02 | P3 | **FIXED** | The duplicate `#ifndef CHROMAWOTD_HOSTNAME` in `net_impl_esp32.cpp` is gone; `config_compiletime.h` owns the default. |
| INC-03 | P3 | **FIXED** | Host `net.cpp` now reads `cc_configActive()` like the device path does. |
| INC-04 | P3 | **FIXED** | The SSID input echoes its value (HTML-escaped) like the other fields, so a validation failure no longer empties it. |
| INC-05 | P3 | **FIXED** | `WiFi.softAPdisconnect(true)` on portal exit. |
| INC-06 | P3 | **FIXED (reworked)** | Auto-detect the first serial port, but **fall back to a named port and wait** when none is present. The suggestion taken literally — resolve at startup, error when nothing is detected — broke BOTH modes on a sleeping device, which is their normal state (the port does not exist until the device wakes). Caught when the tool was first run for real; see §Extra. |
| DOC-01 | P2 | **FIXED** | `docs/STATUS.md` header, the stale "Next steps" list and the false "hardcoded fixture / never sleeps" line corrected; a review-fix row added. |
| DOC-02 | P2 | **FIXED** | `docs/ARCHITECTURE.md` header, the button-toggle and `secrets.h` claims, the weather URL/`timezone` description and the CA description. |
| DOC-03 | P3 | **FIXED** | Both `[eClock](../eClock)` links — line 7 **and line 284**, which this review did not cite — de-linked. |

### Extra findings produced while verifying (beyond this review)

- **`docs/ARCHITECTURE.md` §4 was stale too.** It described "fallback scripture text", a
  defaulted `temp = 0.0f` / `"Temp 0"` condition and an `OFFLINE: weather API` banner — all
  behaviour the code had already deleted when it stopped inventing data. This review cited only
  the header's button-toggle (`:12`) and `secrets` (`:78`) claims, so the failure-mode section
  kept describing a product that no longer exists. Rewritten against `main.cpp`.
- **`ARCHITECTURE.md:127` described the default timezone as a POSIX string**, where the stored
  value is an IANA name and the POSIX rule is derived. Corrected.
- **The Amazon CA in `net_impl_esp32.cpp` was the cross-signed variant** (issuer = Starfield
  Services Root CA G2), which strict verification rejects — "unable to get issuer certificate" —
  because the server never sends that root. mbedTLS had tolerated it by matching the public key,
  so it worked on hardware while being wrong. Replaced with the self-signed Amazon Root CA 1.
- **The `> 0` checks on the other NVS string keys were also wrong** (the mirror image of
  BUG-05): a legitimately empty value — an open network's blank passphrase — makes `putBytes()`
  return 0 and would have been reported as a write failure.
- **INC-06 was implemented as written and it broke the tool.** The finding was right about the
  smell and wrong about the fix: `bench_watch.py`'s modes are built to WAIT for a port that does
  not exist while the device deep-sleeps (`--presence` treats absence as the sleep signal,
  `--capture` waits for the port to appear). Erroring out when nothing was detected therefore
  failed in exactly the state the tool observes. Reworked to prefer a detected port and
  otherwise fall back to a named one and keep waiting (`FALLBACK_PORT`). Re-verified in the
  failing state: with no port present the tool waits and exits 0. See LESSONS §47 — a reminder
  that a finding's *suggested remediation* deserves the same scrutiny as its diagnosis.

---

## Detailed Findings & Remediation Plans

### 1. Software Bugs & Functional Defects

#### BUG-01 (P0): Setup Portal Window Expiry Fall-Through
- **File:** [`firmware/src/main.cpp:376-384`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L376-L384)
- **Problem:**
  When `cc_portalRun()` returns `false` (the 10-minute setup window elapsed without configuration), the code logs:
  ```cpp
  Serial.println("setup: window expired, sleeping (press a button to retry)");
  ```
  However, there is no sleep call or `return`. Execution falls through into the normal sync phase:
  ```cpp
  g_syncDone = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(syncTask, "cc_sync", 16384, nullptr, 1, nullptr, 1);
  xSemaphoreTake(g_syncDone, portMAX_DELAY);
  ```
  Because the device is unprovisioned, `syncTask` fails to connect to Wi-Fi (`cfg.ssid[0] == '\0'`), marks weather and scripture as failed, renders an **OFFLINE: no wifi** screen, and performs a 25-second pigment update on the ePaper!
- **Impact:** The ePaper display had already painted the setup QR code and portal credentials, which stay visible at zero power. This fall-through wipes the setup screen and leaves an error screen instead.
- **Remediation:**
  In `main.cpp`, immediately after `Serial.println("setup: window expired...")`, arm deep sleep timer/button wake and call `esp_deep_sleep_start()` directly.

---

#### BUG-02 (P1): `contentMode` Configuration Ignored
- **File:** [`firmware/src/main.cpp:184`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L184)
- **Problem:**
  `DeviceConfig::contentMode` supports three values: `0 = time-based (default)`, `1 = force verse`, `2 = force word`. The captive portal form presents this selection, `config.cpp` validates it, and `config_nvs.cpp` saves it to flash.
  Yet in `firmware/src/main.cpp:184`:
  ```cpp
  g_mode = g_haveTime ? cc_contentModeForHour(tmv.tm_hour) : ContentMode::Verse;
  ```
  `g_cfg.contentMode` is never checked.
- **Impact:** Users selecting "Verse of the Day only" or "Word of the Day only" in the setup portal are ignored; the device always runs in time-based mode.
- **Remediation:**
  Respect `g_cfg.contentMode`:
  ```cpp
  if (g_cfg.contentMode == 1) {
      g_mode = ContentMode::Verse;
  } else if (g_cfg.contentMode == 2) {
      g_mode = ContentMode::Word;
  } else {
      g_mode = g_haveTime ? cc_contentModeForHour(tmv.tm_hour) : ContentMode::Verse;
  }
  ```

---

#### BUG-03 (P1): Setup Portal Initial Candidate Coordinates Zeroed
- **File:** [`firmware/src/net/portal.cpp:63, 254`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/portal.cpp#L63-L254)
- **Problem:**
  `g_candidate` is a zero-initialized static global. When `cc_portalRun()` starts, `g_candidate` is not seeded with the defaults. On first load of `http://192.168.4.1/`, `page()` renders:
  ```html
  <label>Latitude</label><input name='lat' value='0.0000'>
  <label>Longitude</label><input name='lon' value='0.0000'>
  ```
  In `cc_configValidate()`, coordinates `(0, 0)` are explicitly rejected:
  ```cpp
  else if (cfg.latitude == 0.0f && cfg.longitude == 0.0f) {
      reason = "Latitude and longitude cannot both be 0 - please enter your location.";
  }
  ```
- **Impact:** If a user scans the QR code, enters their Wi-Fi password, and taps Save without altering coordinates, validation fails and rejects the configuration.
- **Remediation:**
  Initialize `g_candidate = cc_configActive();` at the beginning of `cc_portalRun()`. This ensures the form pre-populates the compiled defaults (or prior settings) for coordinates, timezone, and hostname.

---

#### BUG-04 (P2): `test_portal_e2e.py` Rejection on Default Timezone
- **File:** [`tools/test_portal_e2e.py:91`](file:///C:/Users/jptra/Projects/ChromaWOTD/tools/test_portal_e2e.py#L91)
- **Problem:**
  `test_portal_e2e.py` still defines:
  ```python
  ap.add_argument("--tz", default="AEST-10AEDT,M10.1.0,M4.1.0/3")
  ```
  Commit `db5e211` made timezone validation require an IANA name (`Australia/Melbourne`) and explicitly reject bare POSIX strings.
- **Impact:** Running `python tools/test_portal_e2e.py --image ...` with default parameters fails validation with `"Unrecognised timezone. Choose one from the list (e.g. Australia/Melbourne)."`.
- **Remediation:**
  Change the default in `test_portal_e2e.py` to `default="Australia/Melbourne"`.

---

#### BUG-05 (P2): Unsigned Check Masks NVS Write Failures
- **File:** [`firmware/src/config/config_nvs.cpp:93, 95, 96`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/config/config_nvs.cpp#L93-L96)
- **Problem:**
  Lines 93, 95, and 96 do:
  ```cpp
  ok &= p.putBytes(kKeyPass, cfg.passphrase, strnlen(cfg.passphrase, sizeof(cfg.passphrase))) >= 0;
  ok &= p.putBytes(kKeyHost, cfg.hostname, strnlen(cfg.hostname, sizeof(cfg.hostname))) >= 0;
  ok &= p.putBytes(kKeyVer, cfg.configuredBy, strnlen(cfg.configuredBy, sizeof(cfg.configuredBy))) >= 0;
  ```
  `Preferences::putBytes()` returns `size_t` (unsigned). An unsigned number is always `>= 0`. If `putBytes()` fails (returning 0), the expression evaluates to `true`.
- **Impact:** Failed writes to passphrase, hostname, or version keys silently pass as successful.
- **Remediation:**
  If the string is non-empty, require `> 0`. If empty, handle deletion explicitly via `p.remove(key)`.

---

#### BUG-06 (P1): Stored Config Loss via Shared 20 KB NVS Partition
- **Reference:** [`docs/lessons/LESSONS_LEARNT.md:1027`](file:///C:/Users/jptra/Projects/ChromaWOTD/docs/lessons/LESSONS_LEARNT.md#L1027) (LESSONS §45)
- **Problem:**
  The project has no custom `partitions.csv`. PlatformIO uses the default ESP32-S3 partition table, which allocates a single 20 KB `nvs` partition shared between the Arduino Wi-Fi/BLE/DHCP stack and ChromaWOTD's `chromawotd` namespace.
  When the Wi-Fi stack fills the free pages of the 20 KB partition, Arduino's `initArduino()` calls `esp_partition_erase_range(...)` to reformat the entire NVS, wiping user credentials and forcing the device back into setup mode.
- **Remediation:**
  1. Add `firmware/partitions.csv` defining a dedicated NVS partition for configuration (e.g. `nvs_cfg, data, nvs, 0x14000, 0x4000`).
  2. Point `platformio.ini` to `board_build.partitions = partitions.csv`.
  3. Initialize with `Preferences::begin("chromawotd", false, "nvs_cfg")`.

---

### 2. Design & Architecture Issues

#### DES-01 (P1): Pinned Let's Encrypt Intermediate Certificates
- **File:** [`firmware/src/net/net_impl_esp32.cpp:67-116`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/net/net_impl_esp32.cpp#L67-L116)
- **Details:**
  `ROOT_CA_YR2` and `ROOT_CA_YE1` are Let's Encrypt *intermediate* certificates, valid only until September 2, 2028.
  Let's Encrypt rotates intermediate issuing certificates regularly. If Open-Meteo or Wordsmith renews with a leaf certificate signed by a different intermediate (e.g., `YR1` or an alternative ISRG intermediate), the device will fail TLS verification and permanently lose weather or word updates.
- **Recommendation:**
  Pin the actual Root CA: **ISRG Root X1** (RSA) and **ISRG Root X2** / **ISRG Root YR** (ECDSA), which are root trust anchors valid through 2035–2040.

---

#### DES-02 (P2): TLS Verification Depends on NTP Time Sync
- **File:** [`firmware/src/main.cpp:170-220`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/main.cpp#L170-L220)
- **Details:**
  `WiFiClientSecure` validates certificate `notBefore` and `notAfter` against the system clock. If NTP times out (e.g., UDP port 123 blocked, slow DNS), the ESP32 clock stays at 1970-01-01.
  Every TLS handshake will fail verification (`MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`) because 1970 is prior to certificate issuance dates (2025/2026).
- **Recommendation:**
  In `syncTask()`, if `!g_haveTime`, log an explicit warning explaining that TLS certificate validation will likely fail due to lack of wall time, and differentiate NTP timeout from API outages in `g_partialReason`.

---

#### DES-03 (P3): NVS Empty String Clearing
- **File:** [`firmware/src/config/config_nvs.cpp:84-104`](file:///C:/Users/jptra/Projects/ChromaWOTD/firmware/src/config/config_nvs.cpp#L84-L104)
- **Details:**
  When saving configuration, `p.putBytes()` is given `strnlen(str)`. If a user clears their Wi-Fi passphrase (for an open network) or clears their custom hostname, length is 0.
  Calling `putBytes(..., 0)` does not delete an existing key from NVS.
- **Recommendation:**
  If `strlen(value) == 0`, call `p.remove(key)` to erase any previously stored value.

---

### 3. Inconsistencies & Code Smells

1. **Stale Button Wake Comment (`firmware/src/main.cpp:496`):**
   Comment notes `BUTTON1/2/3 = GPIO2/3/5`. GPIO 5 was the old incorrect schematic SDA net that caused deep-sleep wake storms (LESSONS §34). Hardware-verified pins are GPIO 2, 3, 8.
2. **Duplicate Macro (`firmware/src/net/net_impl_esp32.cpp:289`):**
   `CHROMAWOTD_HOSTNAME` is defined in `config_compiletime.h` and redundantly checked/defined in `net_impl_esp32.cpp`.
3. **Coordinates Source Discrepancy (`firmware/src/net/net.cpp:549` vs `firmware/src/net/net_impl_esp32.cpp:194`):**
   Device uses `cc_configActive().latitude/longitude` for Open-Meteo URL; host `net.cpp` formats `CHROMAWOTD_LATITUDE/CHROMAWOTD_LONGITUDE` directly.
4. **Portal SSID Field Value Missing (`firmware/src/net/portal.cpp:121`):**
   `<input name='ssid'>` does not include `value='...'`. On validation failure of other fields, the user's typed SSID is lost.
5. **SoftAP Radio Cleanup (`firmware/src/net/portal.cpp:312-315`):**
   `WiFi.softAPdisconnect(true)` should be called when exiting `cc_portalRun()`.
6. **Hardcoded Port in Bench Tool (`tools/bench_watch.py:37`):**
   `DEFAULT_PORT = "COM13"` should fall back to auto-detecting serial ports via `list_serials()`.

---

### 4. Documentation Drift & Gaps

1. **`docs/STATUS.md`:**
   - Outdated header states: `Phase: 1 — Display bring-up & Layout Prototype (complete)`.
   - "Next steps" section lists tasks that are already fully completed (Dual button controls, NTP sync, Open-Meteo fetch, BibleGateway fetch).
   - Contains incorrect claim: `main.cpp still ships a hardcoded fixture and never sleeps; both are Phase 2/4 work.`.
2. **`docs/ARCHITECTURE.md`:**
   - Header is dated 2026-09-12 with Phase 1 status.
   - States content is "toggled via button" (superseded by time-of-day policy).
   - Refers to loading credentials from "secrets" (which was removed in favor of portal NVS).
3. **`README.md`:**
   - Broken relative link `[eClock](../eClock)` on line 7.

---

## Conclusion & Recommended Action Plan

The core layout and graphic rendering engine is exceptionally solid, with outstanding host testing, regression ledgers, and font rendering. To bring the device firmware to production quality:

1. **Immediate Patch:**
   - Fix `main.cpp` portal timeout fall-through (BUG-01).
   - Hook up `g_cfg.contentMode` in `main.cpp` (BUG-02).
   - Initialize `g_candidate = cc_configActive();` in `portal.cpp` (BUG-03).
   - Update `tools/test_portal_e2e.py` default timezone to `"Australia/Melbourne"` (BUG-04).
   - Correct `config_nvs.cpp` return checks (BUG-05).
2. **Reliability & Security:**
   - Introduce `firmware/partitions.csv` with a dedicated NVS partition for settings (BUG-06).
   - Update Let's Encrypt certificates in `net_impl_esp32.cpp` from intermediates to Root CAs (DES-01).
3. **Documentation Alignment:**
   - Update `docs/STATUS.md`, `docs/ARCHITECTURE.md`, and `README.md` to reflect current Phase 5 reality.
