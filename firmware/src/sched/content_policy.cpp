// content_policy.cpp — see content_policy.h.
#include "sched/content_policy.h"

ContentMode cc_contentModeForMinutes(int minutesOfDay) {
    return (minutesOfDay < kCcWordOfDayStartMinutes) ? ContentMode::Verse : ContentMode::Word;
}

ContentMode cc_resolveContentMode(uint8_t configuredMode, bool haveTime, int minutesOfDay) {
    // A forced mode from the setup portal overrides the time-of-day rule entirely.
    if (configuredMode == 1)
        return ContentMode::Verse;
    if (configuredMode == 2)
        return ContentMode::Word;
    return haveTime ? cc_contentModeForMinutes(minutesOfDay) : ContentMode::Verse;
}

ContentMode cc_invertContentMode(ContentMode m) {
    return (m == ContentMode::Verse) ? ContentMode::Word : ContentMode::Verse;
}

bool cc_useTomorrowForecast(int hour24) {
    return hour24 >= 18;
}
