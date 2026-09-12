// wake_schedule.h — daily refresh schedule for CHROMAWOTD.
//
// PURE (no ESP32/Arduino headers): given the current local time, return the
// number of seconds until the next scheduled refresh. Host-testable; the
// deep-sleep glue (esp_sleep_enable_timer_wakeup) lives in main.cpp.
//
// The device refreshes at three fixed local slots — morning, midday, evening —
// and stays asleep through the night, so a full ~25 s pigment sweep happens
// 3×/day instead of continuously.

#pragma once
#include <time.h>

struct WakeSlot { int hour; int minute; };

// Scheduled refresh slots, local time (order matters: ascending).
extern const WakeSlot kCcWakeSlots[];
extern const int      kCcWakeSlotCount;

// Seconds from `now` to the next scheduled slot. Always > 0: if `now` is at or
// past the last slot of the day, it rolls over to tomorrow's first slot.
int cc_secondsUntilNextWake(const struct tm& now);
