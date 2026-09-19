// hold_gesture.cpp — see hold_gesture.h.
#include "sched/hold_gesture.h"

void cc_holdBegin(HoldState* st) {
    if (!st)
        return;
    st->lowMs = 0;
    st->decided = false;
    st->firedShort = false;
}

HoldGesture cc_holdFeed(HoldState* st, bool pressed, uint32_t dtMs) {
    if (!st || st->decided)
        return HoldGesture::None;

    if (pressed)
        st->lowMs += dtMs;

    if (!pressed) {
        // Released: it is a Tap only if the hold never reached the ShortHold threshold.
        // After a reported ShortHold the release simply ends the gesture.
        st->decided = true;
        if (st->firedShort || st->lowMs >= CC_HOLD_SHORT_MS)
            return HoldGesture::None;
        return HoldGesture::Tap;
    }

    if (st->lowMs >= CC_HOLD_RESET_MS) {
        st->decided = true;
        return HoldGesture::Reset;
    }

    if (!st->firedShort && st->lowMs >= CC_HOLD_SHORT_MS) {
        st->firedShort = true;
        return HoldGesture::ShortHold;
    }

    return HoldGesture::None;
}

void cc_holdSessionBegin(HoldSession* s) {
    if (!s)
        return;
    cc_holdBegin(&s->st);
    s->pending = HoldGesture::None;
    s->havePending = false;
}

HoldGesture cc_holdSessionFeed(HoldSession* s, bool pressed, uint32_t dtMs) {
    if (!s)
        return HoldGesture::None;
    const HoldGesture g = cc_holdFeed(&s->st, pressed, dtMs);
    if (g == HoldGesture::ShortHold) {
        // Reported while still held: LATCH it and keep watching for a Reset.
        s->pending = g;
        s->havePending = true;
        return HoldGesture::None;
    }
    if (g != HoldGesture::None)
        return g; // Tap or Reset: final
    if (s->st.decided)
        return s->havePending ? s->pending : HoldGesture::None; // the release finalises it
    return HoldGesture::None;
}

int cc_holdProgress(const HoldState& st) {
    if (CC_HOLD_RESET_MS == 0)
        return 100;
    const uint32_t pct = (st.lowMs * 100u) / CC_HOLD_RESET_MS;
    return pct > 100u ? 100 : (int)pct;
}
