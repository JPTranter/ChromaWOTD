// content_policy.cpp — see content_policy.h.
#include "sched/content_policy.h"

ContentMode cc_contentModeForHour(int hour24) {
    return (hour24 < 12) ? ContentMode::Verse : ContentMode::Word;
}

bool cc_useTomorrowForecast(int hour24) {
    return hour24 >= 18;
}
