// weather_icon.cpp — vector weather icons. See weather_icon.h for the contract.

#include "weather_icon.h"

void drawWeatherIcon(DisplayTarget& t, int cx, int cy, int size, WeatherIcon iconType) {
    int r = size / 3;
    if (r < 3) r = 3;

    uint32_t cloudOutline = CC_BLACK;
    uint32_t cloudFill    = CC_WHITE;

    switch (iconType) {
        case WeatherIcon::Sun: { // Sun
            t.fillCircle(cx, cy, r, CC_YELLOW);
            t.drawCircle(cx, cy, r, CC_YELLOW);
            // 8 Rays
            int r1 = r + 2;
            int r2 = size / 2;
            t.drawLine(cx, cy - r1, cx, cy - r2, CC_YELLOW);
            t.drawLine(cx, cy + r1, cx, cy + r2, CC_YELLOW);
            t.drawLine(cx - r1, cy, cx - r2, cy, CC_YELLOW);
            t.drawLine(cx + r1, cy, cx + r2, cy, CC_YELLOW);
            int d1 = (int)(r1 * 0.707f);
            int d2 = (int)(r2 * 0.707f);
            t.drawLine(cx - d1, cy - d1, cx - d2, cy - d2, CC_YELLOW);
            t.drawLine(cx + d1, cy - d1, cx + d2, cy - d2, CC_YELLOW);
            t.drawLine(cx - d1, cy + d1, cx - d2, cy + d2, CC_YELLOW);
            t.drawLine(cx + d1, cy + d1, cx + d2, cy + d2, CC_YELLOW);
            break;
        }
        case WeatherIcon::Cloud: { // Cloud
            t.fillCircle(cx - size / 4, cy + size / 10, size / 5, cloudFill);
            t.drawCircle(cx - size / 4, cy + size / 10, size / 5, cloudOutline);
            t.fillCircle(cx + size / 5, cy + size / 10, size / 6, cloudFill);
            t.drawCircle(cx + size / 5, cy + size / 10, size / 6, cloudOutline);
            t.fillCircle(cx, cy - size / 10, size / 4, cloudFill);
            t.drawCircle(cx, cy - size / 10, size / 4, cloudOutline);
            t.fillRect(cx - size / 4, cy - size / 10, size / 2, size / 3, cloudFill);
            t.drawFastHLine(cx - size / 3, cy + size / 4, (size * 2) / 3, cloudOutline);
            break;
        }
        case WeatherIcon::Rain: { // Rain
            // cyShift and the drop x-offset must scale with size so the glyph stays
            // proportional when the icon is reflowed larger (see drawLandscapeWeatherColumn).
            int cyShift = cy - size / 6;   // == cy-4 at size 24
            t.fillCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudFill);
            t.drawCircle(cx - size / 4, cyShift + size / 10, size / 5, cloudOutline);
            t.fillCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudFill);
            t.drawCircle(cx + size / 5, cyShift + size / 10, size / 6, cloudOutline);
            t.fillCircle(cx, cyShift - size / 10, size / 4, cloudFill);
            t.drawCircle(cx, cyShift - size / 10, size / 4, cloudOutline);
            t.fillRect(cx - size / 4, cyShift - size / 10, size / 2, size / 3, cloudFill);
            t.drawFastHLine(cx - size / 3, cyShift + size / 4, (size * 2) / 3, cloudOutline);
            // Red rain drops (offsets proportional to size; == 6/3/9 at size 24)
            int dx = size / 4;
            int dropTop = cyShift + size / 4 + 3;
            int dropBot = cyShift + size / 2 + 3;
            t.drawLine(cx - dx, dropTop, cx - dx - 3, dropBot, CC_RED);
            t.drawLine(cx,      dropTop, cx - 3,       dropBot, CC_RED);
            t.drawLine(cx + dx, dropTop, cx + dx - 3, dropBot, CC_RED);
            break;
        }
        case WeatherIcon::PartlyCloudy: // Partly cloudy
        default: {
            // Sun peeking behind cloud (stub rays scale with size so the glyph
            // stays proportional on the larger reflowed icon)
            int ray = size / 8;   // == 3 at size 24
            if (ray < 3) ray = 3;
            t.fillCircle(cx - size / 4, cy - size / 5, size / 4, CC_YELLOW);
            t.drawLine(cx - size / 4, cy - size / 5 - size / 4 - ray, cx - size / 4, cy - size / 5 - size / 4 - ray + 2, CC_YELLOW);
            t.drawLine(cx - size / 4 - size / 4 - ray, cy - size / 5, cx - size / 4 - size / 4 - ray + 2, cy - size / 5, CC_YELLOW);
            // Masking cloud in foreground
            t.fillCircle(cx - size / 6, cy + size / 8, size / 5, cloudFill);
            t.drawCircle(cx - size / 6, cy + size / 8, size / 5, cloudOutline);
            t.fillCircle(cx + size / 5, cy + size / 8, size / 6, cloudFill);
            t.drawCircle(cx + size / 5, cy + size / 8, size / 6, cloudOutline);
            t.fillCircle(cx + 2, cy - size / 12, size / 4, cloudFill);
            t.drawCircle(cx + 2, cy - size / 12, size / 4, cloudOutline);
            t.fillRect(cx - size / 6, cy - size / 12, (size * 5) / 12, size / 3, cloudFill);
            t.drawFastHLine(cx - size / 4, cy + size / 4, (size * 7) / 12, cloudOutline);
            break;
        }
    }
}
