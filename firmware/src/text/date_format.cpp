// date_format.cpp — see date_format.h.
#include "text/date_format.h"

#include <cstdio>

namespace {

// Locale-independent, index-aligned with tm_wday / tm_mon.
const char* const kDow[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

} // namespace

void cc_formatHeaderDate(int wday, int day, int month, char* out, size_t outsz) {
    if (!out || outsz == 0)
        return;
    const char* dow = (wday >= 0 && wday < 7) ? kDow[wday] : "---";
    const char* mon = (month >= 1 && month <= 12) ? kMon[month - 1] : "---";
    if (day < 1 || day > 31)
        snprintf(out, outsz, "%s -- %s", dow, mon);
    else
        snprintf(out, outsz, "%s %02d %s", dow, day, mon); // "Fri 09 Sep"
}
