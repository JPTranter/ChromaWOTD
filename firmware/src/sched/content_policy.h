// content_policy.h — time-of-day presentation policy for ChromaWOTD.
//
// PURE (no Arduino/ESP32 headers): given the local hour, decide which content to
// show and whether the weather column shows today's or tomorrow's outlook.
// Host-testable; the device just calls these with the NTP-synced local hour.

#pragma once

#include <cstdint>

enum class ContentMode { Verse, Word };

// The MINUTE of the day the Word of the Day window opens: 16:30.
//
// Chosen against the SOURCE's publish instant, not the local calendar. A.Word.A.Day publishes at
// 00:01 US Eastern, which in Melbourne is 14:01 AEST / 15:01 AEDT / **16:01 AEDT while the US is on
// standard time** — the last of those is the worst case and it is not hypothetical (it covers
// November to March). 16:30 clears it by 29 minutes in every month of the year; the earlier 15:00
// boundary did not, which is the reason this is expressed in minutes rather than hours: an
// hour-only boundary cannot clear a 16:01 flip (LESSONS §70).
//
// The page's own edition date is STILL compared against the local date (main.cpp): the clock decides
// when to look, the data decides whether what came back is today's.
constexpr int kCcWordOfDayStartMinutes = 16 * 60 + 30; // 16:30 local

// minutesOfDay = hour*60 + minute, local time.
// 00:00–16:29 -> Verse of the Day; 16:30–23:59 -> Word of the Day.
ContentMode cc_contentModeForMinutes(int minutesOfDay);

// Resolve which content to show, honouring the portal's forced-mode setting:
//   0 = time-based (the default), 1 = force verse, 2 = force word.
// Any other value is treated as 0. A forced mode wins regardless of the clock;
// the time-based rule needs a valid clock, so without one it falls back to the
// verse (showing the wrong half of the day's content is worse than always
// showing scripture until NTP succeeds).
ContentMode cc_resolveContentMode(uint8_t configuredMode, bool haveTime, int minutesOfDay);

// The wake gesture's temporary override: a SHORT HOLD inverts whatever the rules above
// decided, so the user can see the other content without changing any setting. Transient by
// design — the caller clears it at the next SCHEDULED wake, so the device can never be left
// stuck showing the wrong half of the day.
ContentMode cc_invertContentMode(ContentMode m);

// Evening (18:00–23:59) shows TOMORROW's outlook; earlier hours show today's.
// This applies to button-triggered syncs too, not just the 18:00 slot.
bool cc_useTomorrowForecast(int hour24);
