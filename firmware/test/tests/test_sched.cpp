// test_sched.cpp — wake-schedule arithmetic (pure, always run).
//
// Locks the boundary behaviour of the 06:30 / 12:30 / 18:00 refresh slots: the
// next-slot computation must be strictly forward, roll over midnight, and never
// return 0 (which would spin the device in a wake loop).
#include "sched/wake_schedule.h"
#include <gtest/gtest.h>

static struct tm at(int h, int m, int s = 0) {
    struct tm t{};
    t.tm_hour = h;
    t.tm_min  = m;
    t.tm_sec  = s;
    return t;
}

TEST(WakeSchedule, SlotsAreThreeAscending) {
    ASSERT_EQ(kCcWakeSlotCount, 3);
    EXPECT_EQ(kCcWakeSlots[0].hour, 6);  EXPECT_EQ(kCcWakeSlots[0].minute, 30);
    EXPECT_EQ(kCcWakeSlots[1].hour, 12); EXPECT_EQ(kCcWakeSlots[1].minute, 30);
    EXPECT_EQ(kCcWakeSlots[2].hour, 18); EXPECT_EQ(kCcWakeSlots[2].minute, 0);
}

TEST(WakeSchedule, Midnight_ToMorning) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(0, 0)), 6 * 3600 + 30 * 60);
}

TEST(WakeSchedule, BeforeMorningSlot) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(6, 0)), 1800);   // 06:00 -> 06:30
}

TEST(WakeSchedule, ExactlyOnMorningSlot_GoesToMidday) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(6, 30)), 6 * 3600);   // 06:30 -> 12:30
}

TEST(WakeSchedule, Midday_To_Evening) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(12, 30)), 5 * 3600 + 30 * 60);
}

TEST(WakeSchedule, ExactlyOnEveningSlot_RollsToTomorrowMorning) {
    // 18:00 -> 24:00 (6 h) + 06:30 (6.5 h) = 12.5 h
    EXPECT_EQ(cc_secondsUntilNextWake(at(18, 0)), 12 * 3600 + 30 * 60);
}

TEST(WakeSchedule, Night_RollsOver) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(23, 0)), 7 * 3600 + 30 * 60);   // -> 06:30
}

TEST(WakeSchedule, AlwaysPositive_EvenAtLastSecondOfDay) {
    EXPECT_GT(cc_secondsUntilNextWake(at(23, 59, 59)), 0);
}
