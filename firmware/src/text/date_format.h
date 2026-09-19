// date_format.h — the header's date string, formatted in ONE place so the render path
// and the device cannot disagree about it.
//
// Format: DOW DD MMM, e.g. "Fri 19 Sep".
//
// Why a table rather than strftime("%a %d %b"): newlib's %a/%b are LOCALE-dependent and
// this device never sets a locale, so the same code could render differently under a
// different TZ/locale. A table is deterministic and host-testable.
//
// Why it lives here at all: the renders used to be given a hand-written date string
// ("Fri, Sep 12") while the device built its own ("2026-09-19"), so a render could look
// right while the panel was wrong. Both paths call this now.
#pragma once

#include <cstddef>

// Format the header date. `wday` is tm_wday (0 = Sunday), `month` is tm_mon + 1 (1-12),
// `day` is tm_mday. Out-of-range input yields a neutral "---" token rather than printing
// nonsense. Always NUL-terminates (needs at most 13 bytes: "Wed 09 Sep" + NUL).
void cc_formatHeaderDate(int wday, int day, int month, char* out, size_t outsz);
