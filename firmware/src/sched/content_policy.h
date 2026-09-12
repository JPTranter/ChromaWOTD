// content_policy.h — time-of-day presentation policy for CHROMAWOTD.
//
// PURE (no Arduino/ESP32 headers): given the local hour, decide which content to
// show and whether the weather column shows today's or tomorrow's outlook.
// Host-testable; the device just calls these with the NTP-synced local hour.

#pragma once

enum class ContentMode { Verse, Word };

// 00:00–11:59 -> Verse of the Day; 12:00–23:59 -> Word of the Day.
ContentMode cc_contentModeForHour(int hour24);

// Evening (18:00–23:59) shows TOMORROW's outlook; earlier hours show today's.
// This applies to button-triggered syncs too, not just the 18:00 slot.
bool cc_useTomorrowForecast(int hour24);
