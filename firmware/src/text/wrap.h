// wrap.h — line-capacity and width-budget math for ChromaWOTD's greedy text
// wrapper. Pure (no font/backend state) so it can be unit-tested in isolation;
// the caller supplies maxH/size/lineHeight/maxW/charWidth already resolved.

#pragma once

// How many lines fit in maxH at this line height.
int cc_lineCapacity(int maxH, int size, int lineHeight);

// Width budget for one line. When the text will be cut off, the final line is
// kept short enough that the "..." marker still fits inline: 6 glyph widths of
// slack for centred text (the line floats, so both margins count).
int cc_lineBudget(int maxW, int charWidth, bool truncated, int lineIdx, int capacity);
