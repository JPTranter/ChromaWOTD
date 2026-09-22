// content_policy.h — time-of-day presentation policy for ChromaWOTD.
//
// PURE (no Arduino/ESP32 headers): given the local hour, decide which content to
// show and whether the weather column shows today's or tomorrow's outlook.
// Host-testable; the device just calls these with the NTP-synced local hour.

#pragma once

#include <cstdint>

enum class ContentMode { Verse, Word };

// The local hour the Word of the Day window opens (15:00). Chosen against the SOURCE's
// publish instant rather than the local calendar: A.Word.A.Day publishes at 00:01 US
// Eastern, which is 14:01 AEST / 15:01 AEDT / 16:01 AEDT-with-US-standard-time. 15:00 is
// therefore past the flip for the common case and within an hour of it at worst — and the
// page's own edition date is still compared against the local date (main.cpp), so a word
// that is not yet today's falls back to the verse instead of repeating yesterday's.
// Single source of truth: the tests and tools/font_size_probe.py cite this constant.
constexpr int kCcWordOfDayStartHour = 15;

// 00:00–14:59 -> Verse of the Day; 15:00–23:59 -> Word of the Day.
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
