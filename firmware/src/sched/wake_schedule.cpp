// wake_schedule.cpp — see wake_schedule.h.
#include "sched/wake_schedule.h"

const WakeSlot kCcWakeSlots[] = {
    { 6, 30},   // morning
    {12, 30},   // midday
    {18,  0},   // evening
};
const int kCcWakeSlotCount = sizeof(kCcWakeSlots) / sizeof(kCcWakeSlots[0]);

int cc_secondsUntilNextWake(const struct tm& now) {
    const int nowSec = now.tm_hour * 3600 + now.tm_min * 60 + now.tm_sec;

    for (int i = 0; i < kCcWakeSlotCount; i++) {
        const int slotSec = kCcWakeSlots[i].hour * 3600 + kCcWakeSlots[i].minute * 60;
        if (slotSec > nowSec) return slotSec - nowSec;   // strictly next slot
    }

    // Past the last slot of the day -> roll over to tomorrow's first slot.
    const int firstSec = kCcWakeSlots[0].hour * 3600 + kCcWakeSlots[0].minute * 60;
    return (24 * 3600 - nowSec) + firstSec;
}
