// test_sched.cpp — wake-schedule arithmetic (pure, always run).
//
// Locks the boundary behaviour of the 06:00 / 12:30 / 18:00 refresh slots: the
// next-slot computation must be strictly forward, roll over midnight, and never
// return 0 (which would spin the device in a wake loop).
#include "sched/wake_schedule.h"
#include "sched/content_policy.h"
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
    EXPECT_EQ(kCcWakeSlots[0].hour, 6);  EXPECT_EQ(kCcWakeSlots[0].minute, 0);
    EXPECT_EQ(kCcWakeSlots[1].hour, 12); EXPECT_EQ(kCcWakeSlots[1].minute, 30);
    EXPECT_EQ(kCcWakeSlots[2].hour, 18); EXPECT_EQ(kCcWakeSlots[2].minute, 0);
}

TEST(WakeSchedule, Midnight_ToMorning) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(0, 0)), 6 * 3600);   // -> 06:00
}

TEST(WakeSchedule, JustBeforeMorningSlot) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(5, 59)), 60);   // 05:59 -> 06:00
}

TEST(WakeSchedule, ExactlyOnMorningSlot_GoesToMidday) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(6, 0)), 6 * 3600 + 30 * 60);   // 06:00 -> 12:30
}

TEST(WakeSchedule, Midday_To_Evening) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(12, 30)), 5 * 3600 + 30 * 60);
}

TEST(WakeSchedule, ExactlyOnEveningSlot_RollsToTomorrowMorning) {
    // 18:00 -> 24:00 (6 h) + 06:00 (6 h) = 12 h
    EXPECT_EQ(cc_secondsUntilNextWake(at(18, 0)), 12 * 3600);
}

TEST(WakeSchedule, Night_RollsOver) {
    EXPECT_EQ(cc_secondsUntilNextWake(at(23, 0)), 7 * 3600);   // -> 06:00
}

TEST(WakeSchedule, AlwaysPositive_EvenAtLastSecondOfDay) {
    EXPECT_GT(cc_secondsUntilNextWake(at(23, 59, 59)), 0);
}

// ---------------------------------------------------------------------------
// Content policy (time-of-day presentation) ---------------------------------
// ---------------------------------------------------------------------------
TEST(ContentPolicy, VerseInTheMorning) {
    for (int h = 0; h < 12; h++)
        EXPECT_EQ(cc_contentModeForHour(h), ContentMode::Verse) << "hour " << h;
}

TEST(ContentPolicy, WordInTheAfternoonAndEvening) {
    for (int h = 12; h < 24; h++)
        EXPECT_EQ(cc_contentModeForHour(h), ContentMode::Word) << "hour " << h;
}

TEST(ContentPolicy, ModeFlipsExactlyAtNoon) {
    EXPECT_EQ(cc_contentModeForHour(11), ContentMode::Verse);
    EXPECT_EQ(cc_contentModeForHour(12), ContentMode::Word);
}

TEST(ContentPolicy, TodayForecastBefore18) {
    for (int h = 0; h < 18; h++)
        EXPECT_FALSE(cc_useTomorrowForecast(h)) << "hour " << h;
}

TEST(ContentPolicy, TomorrowForecastFrom18) {
    for (int h = 18; h < 24; h++)
        EXPECT_TRUE(cc_useTomorrowForecast(h)) << "hour " << h;
}

// --- Forced mode from the setup portal (contentMode) ------------------------
// The portal offers "time-based / verse only / word only". These lock the
// resolution rule so the stored setting cannot silently be ignored again (it was:
// main.cpp read only the clock and never looked at cfg.contentMode).
TEST(ContentPolicy, ForcedVerseOverridesTheClock) {
    for (int h = 0; h < 24; h++)
        EXPECT_EQ(cc_resolveContentMode(1, true, h), ContentMode::Verse) << "hour " << h;
    EXPECT_EQ(cc_resolveContentMode(1, false, 0), ContentMode::Verse); // holds with no clock
}

TEST(ContentPolicy, ForcedWordOverridesTheClock) {
    for (int h = 0; h < 24; h++)
        EXPECT_EQ(cc_resolveContentMode(2, true, h), ContentMode::Word) << "hour " << h;
    EXPECT_EQ(cc_resolveContentMode(2, false, 0), ContentMode::Word);
}

TEST(ContentPolicy, TimeBasedModeFollowsTheHour) {
    for (int h = 0; h < 24; h++)
        EXPECT_EQ(cc_resolveContentMode(0, true, h), cc_contentModeForHour(h)) << "hour " << h;
    // Any out-of-range value is treated as time-based (validation allows 0..2, but
    // NVS is not trusted to hold only what validation accepted).
    EXPECT_EQ(cc_resolveContentMode(7, true, 9), ContentMode::Verse);
    EXPECT_EQ(cc_resolveContentMode(255, true, 20), ContentMode::Word);
}

TEST(ContentPolicy, WithoutAClockTimeBasedModeFallsBackToVerse) {
    for (int h = 0; h < 24; h++)
        EXPECT_EQ(cc_resolveContentMode(0, false, h), ContentMode::Verse) << "hour " << h;
}
