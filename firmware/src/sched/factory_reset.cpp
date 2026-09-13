// factory_reset.cpp — pure long-press accounting. See factory_reset.h for why the
// gesture is sampled rather than timed against millis() directly.
#include "sched/factory_reset.h"

void cc_resetHoldBegin(ResetHoldState* st) {
    if (!st)
        return;
    st->heldSamples = 0;
    st->triggered = false;
}

bool cc_resetHoldSample(ResetHoldState* st, bool pressed) {
    if (!st || st->triggered)
        return false; // already fired: never fire twice
    if (!pressed) {
        st->heldSamples = 0; // released: abandon the gesture
        return false;
    }
    st->heldSamples++;
    if (st->heldSamples >= CC_RESET_SAMPLES) {
        st->triggered = true;
        return true;
    }
    return false;
}

int cc_resetHoldProgress(const ResetHoldState& st) {
    if (CC_RESET_SAMPLES == 0)
        return 100;
    const uint32_t pct = (st.heldSamples * 100u) / CC_RESET_SAMPLES;
    return pct > 100u ? 100 : (int)pct;
}
