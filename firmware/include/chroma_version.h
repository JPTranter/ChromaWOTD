#pragma once

// Firmware version string, reported on boot (`Serial.printf("CHROMAWOTD %s boot")`).
//
// This is the version for a LOCAL build. A release build overrides it: the Release
// workflow injects the git tag, so a released artifact can never claim a version it
// was not built as. Keep this in step with the next tag so a manual build reports
// something meaningful.
//
// HOW TO CUT A RELEASE
//   1. Confirm the tree is green and committed:
//        python tools/verify_all.py --clean      # ALL GREEN required
//   2. Bump the version below to the new semver and commit it with the release work.
//   3. Tag and push — pushing the tag is what triggers the Release workflow:
//        git tag -a v0.1.0 -m "ChromaWOTD v0.1.0"
//        git push origin v0.1.0
//   4. Watch it and confirm the assets:
//        gh run watch --repo JPTranter/ChromaWOTD
//        gh release view v0.1.0 --repo JPTranter/ChromaWOTD
//      Expected assets: ChromaWOTD-v0.1.0.bin (merged, flash at 0x0),
//      ChromaWOTD-v0.1.0-app.bin (app only, 0x10000), ChromaWOTD-v0.1.0.elf.
//
// The release notes are generated from the feat/fix commits since the previous v*
// tag and include the flash procedure, so the Releases page is self-contained.
//
// IMPORTANT — released images carry NO credentials, and cannot. Credentials exist
// only in the device's NVS, written by its setup portal; nothing is compiled in from
// a source file. A freshly flashed device comes up in the setup wizard, where the
// user joins its AP (scannable QR code) and enters their Wi-Fi details.
//
// OTA/signing posture (REVIEW S5): images are currently unsigned. If this ever
// becomes a shared/shipped device, add signed updates before shipping OTA.
#define CHROMAWOTD_VERSION "0.1.1"
