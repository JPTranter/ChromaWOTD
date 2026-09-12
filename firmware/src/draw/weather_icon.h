// weather_icon.h — vector weather icons for CHROMAWOTD.
//
// Drawn as filled/outlined circles, lines and rects through a DisplayTarget, so
// they render identically on device (Seeed GFX) and host (mock canvas). Extracted
// from verse_display.cpp so the icon geometry is its own reviewable unit (D1).

#pragma once

#include "verse_display.h"
#include "draw/target.h"

// Draw a weather icon centred at (cx, cy) with the given size. The four icons
// (Sun, Cloud, Rain, PartlyCloudy) are distinct; see the WeatherIcon enum.
void drawWeatherIcon(DisplayTarget& t, int cx, int cy, int size, WeatherIcon iconType);
