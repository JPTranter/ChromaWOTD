#pragma once

// Firmware version string, reported on boot and (later) in the setup portal.
//
// Release checklist (REVIEW DOC7): before tagging a release —
//   1. Bump CHROMAWOTD_VERSION to the new semver.
//   2. Run `python tools/verify_all.py --clean` and confirm ALL GREEN
//      (firmware build + host tests + render ledger byte-match).
//   3. `git tag v<version>` and push the tag.
//   4. Confirm the tagged CI run builds `firmware.bin` from a clean checkout.
//
// OTA/signing posture (REVIEW S5): images are currently unsigned. If this ever
// becomes a shared/shipped device, add signed updates before shipping OTA.
#define CHROMAWOTD_VERSION "0.1.0"
