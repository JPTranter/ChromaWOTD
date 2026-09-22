// wrap.cpp — line-capacity and width-budget math. See wrap.h for the contract.

#include "wrap.h"

int cc_lineCapacity(int maxH, int lineHeight) {
    if (maxH <= 0 || lineHeight <= 0) return 0;
    return maxH / lineHeight;
}

int cc_lineBudget(int maxW, int charWidth, bool truncated, int lineIdx, int capacity) {
    const int slack = 6;
    if (truncated && capacity > 0 && lineIdx == capacity - 1) {
        int budget = maxW - slack * charWidth;
        // When the column is so narrow that even the reserved slack can't fit,
        // we return maxW (no reservation). In that case drawOverflowMarker's
        // "..." degrades to a single "." — an accepted trade-off: a lone dot is
        // the only marker that fits a sub-4-glyph column, and silently dropping
        // the marker entirely would be worse than an ambiguous ".".
        if (budget >= 4 * charWidth) return budget;
    }
    return maxW;
}
