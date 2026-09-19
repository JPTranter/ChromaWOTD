// hold_gesture.h — classify a button gesture from samples taken at a wake.
//
// The device deep-sleeps between refreshes and the press is what WAKES it, so a gesture
// cannot be a millis() comparison: the firmware has no idea how long the user has been
// holding by the time it boots. It samples instead.
//
// MEASURED ON HARDWARE (LESSONS §58, env:gestureprobe):
//   * after an ext1 wake the pads are observable 84 ms after the app starts (repeatable to
//     1 us across four wakes);
//   * a TAP releases 106-121 ms after that first sample;
//   * a DOUBLE CLICK is NOT usable: every button wake - single tap or double click - shows
//     the pad still low at the first sample, because the first click is over before the
//     firmware can look. Tap and double-click are therefore indistinguishable, which is why
//     this classifier distinguishes by HOLD LENGTH instead.
//
//   released before CC_HOLD_SHORT_MS -> Tap        (sync + refresh; unchanged behaviour)
//   held to CC_HOLD_SHORT_MS        -> ShortHold  (toggle the content mode)
//   held to CC_HOLD_RESET_MS        -> Reset      (wipe stored configuration)
//
// Pure: a sample accumulator, unit-tested without hardware. The sampling loop lives in
// main.cpp, which MUST run it before the serial delay — a tap is over in ~120 ms.
#pragma once

#include <cstdint>

enum class HoldGesture { None, Tap, ShortHold, Reset };

// Held this long -> a deliberate hold. 500 ms is ~4x the measured tap release (106-121 ms),
// so a slower-than-average tap still reads as a tap.
static constexpr uint32_t CC_HOLD_SHORT_MS = 500;
// Held this long -> factory reset (PROJECT_PLAN's 10 s; long enough never to fire on bounce).
static constexpr uint32_t CC_HOLD_RESET_MS = 10000;
// Sampling cadence.
static constexpr uint32_t CC_HOLD_SAMPLE_MS = 50;

struct HoldState {
    uint32_t lowMs;  // ms observed low since the FIRST SAMPLE (not since the physical press)
    bool decided;    // the gesture is over; stop feeding
    bool firedShort; // ShortHold already reported, so a later release is not a Tap
};

// Reset the tracker.
void cc_holdBegin(HoldState* st);

// Feed one sample: `pressed` is the pad level (true == low == pressed), `dtMs` is the time
// since the previous sample. Returns the gesture the moment it is decided, else None.
//
// ShortHold is reported WHILE the pad is still down, because the user cannot be asked to
// release before knowing what happened; the sampling continues afterwards so a hold that
// keeps going can still escalate to Reset, and the eventual release ends the gesture
// quietly rather than as a second (Tap) report.
HoldGesture cc_holdFeed(HoldState* st, bool pressed, uint32_t dtMs);

// 0..100 progress toward the Reset threshold, for optional on-screen feedback while held.
int cc_holdProgress(const HoldState& st);

// --- A whole gesture session ---------------------------------------------------
// Feed samples until the gesture is OVER, and report what it turned out to be.
//
// This exists because the first version of the device loop got it wrong: cc_holdFeed
// reports ShortHold WHILE the pad is still down (so a continuing hold can escalate to a
// Reset), and the loop — which kept watching, correctly — then discarded the pending
// ShortHold and returned None on the release. A short hold was silently ignored on
// hardware while the classifier's own tests were green, because the CONSUMER of a pure
// function is not covered by that function's tests (LESSONS §59).
//
// So the loop lives here, in a unit-tested module, and main.cpp only ticks it.
struct HoldSession {
    HoldState st;
    HoldGesture pending; // ShortHold seen but not yet final (a Reset may still follow)
    bool havePending;
};

void cc_holdSessionBegin(HoldSession* s);

// Feed one sample. Returns the FINAL gesture once the session ends, else None:
//   * Tap      - reported on release before the hold threshold;
//   * ShortHold- reported when the pad is RELEASED after the hold threshold (latched from the
//                moment it was first seen, so a hold that keeps going can still report Reset);
//   * Reset    - reported as soon as the reset threshold is crossed, while still held.
HoldGesture cc_holdSessionFeed(HoldSession* s, bool pressed, uint32_t dtMs);
