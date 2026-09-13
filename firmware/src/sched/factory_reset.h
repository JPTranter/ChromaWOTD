// factory_reset.h — long-press detection for wiping stored configuration.
//
// The device deep-sleeps between refreshes, so a "hold for 10 s" gesture cannot be
// a simple millis() comparison: the button press is what WAKES it, and the firmware
// has no idea how long the user has been holding by the time it boots. The gesture
// is therefore measured in two phases:
//
//   1. On a button wake, sample the button immediately and repeatedly for up to
//      CC_RESET_HOLD_MS. If it is still held at the end, treat it as a factory reset.
//   2. Any earlier release abandons the gesture and proceeds with a normal refresh.
//
// The decision logic is pure (a count of consecutive "still held" samples) so it is
// unit-tested without hardware; the sampling loop lives in main.cpp.
#pragma once

#include <cstdint>

// How long a button must be held to trigger a factory reset (PROJECT_PLAN specifies
// 10 seconds). Long enough not to fire on a bounced or accidental press.
static constexpr uint32_t CC_RESET_HOLD_MS = 10000;

// Sampling cadence for the hold check, and the derived number of samples required.
static constexpr uint32_t CC_RESET_SAMPLE_MS = 50;
static constexpr uint32_t CC_RESET_SAMPLES = (CC_RESET_HOLD_MS + CC_RESET_SAMPLE_MS - 1) / CC_RESET_SAMPLE_MS;

struct ResetHoldState {
    uint32_t heldSamples; // consecutive samples where the button was down
    bool triggered;       // latched once the threshold is met
};

// Reset the tracker to its initial state.
void cc_resetHoldBegin(ResetHoldState* st);

// Feed one sample: `pressed` is the debounced button level (true == held down).
// Returns true exactly once, on the sample that completes the hold, so the caller
// can act precisely once rather than repeatedly.
bool cc_resetHoldSample(ResetHoldState* st, bool pressed);

// Percentage of the required hold completed (0..100), for optional on-screen
// feedback while the user holds the button.
int cc_resetHoldProgress(const ResetHoldState& st);
