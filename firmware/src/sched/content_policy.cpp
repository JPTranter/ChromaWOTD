// content_policy.cpp — see content_policy.h.
#include "sched/content_policy.h"

ContentMode cc_contentModeForHour(int hour24) {
    return (hour24 < 12) ? ContentMode::Verse : ContentMode::Word;
}

ContentMode cc_resolveContentMode(uint8_t configuredMode, bool haveTime, int hour24) {
    // A forced mode from the setup portal overrides the time-of-day rule entirely.
    if (configuredMode == 1)
        return ContentMode::Verse;
    if (configuredMode == 2)
        return ContentMode::Word;
    return haveTime ? cc_contentModeForHour(hour24) : ContentMode::Verse;
}

bool cc_useTomorrowForecast(int hour24) {
    return hour24 >= 18;
}
