// content_policy.h — time-of-day presentation policy for CHROMAWOTD.
//
// PURE (no Arduino/ESP32 headers): given the local hour, decide which content to
// show and whether the weather column shows today's or tomorrow's outlook.
// Host-testable; the device just calls these with the NTP-synced local hour.

#pragma once

#include <cstdint>

enum class ContentMode { Verse, Word };

// 00:00–11:59 -> Verse of the Day; 12:00–23:59 -> Word of the Day.
ContentMode cc_contentModeForHour(int hour24);

// Resolve which content to show, honouring the portal's forced-mode setting:
//   0 = time-based (the default), 1 = force verse, 2 = force word.
// Any other value is treated as 0. A forced mode wins regardless of the clock;
// the time-based rule needs a valid clock, so without one it falls back to the
// verse (showing the wrong half of the day's content is worse than always
// showing scripture until NTP succeeds).
ContentMode cc_resolveContentMode(uint8_t configuredMode, bool haveTime, int hour24);

// The wake gesture's temporary override: a SHORT HOLD inverts whatever the rules above
// decided, so the user can see the other content without changing any setting. Transient by
// design — the caller clears it at the next SCHEDULED wake, so the device can never be left
// stuck showing the wrong half of the day.
ContentMode cc_invertContentMode(ContentMode m);

// Evening (18:00–23:59) shows TOMORROW's outlook; earlier hours show today's.
// This applies to button-triggered syncs too, not just the 18:00 slot.
bool cc_useTomorrowForecast(int hour24);
