// wrap.h — line-capacity and width-budget math for ChromaWOTD's greedy text
// wrapper. Pure (no font/backend state) so it can be unit-tested in isolation;
// the caller supplies maxH/size/lineHeight/maxW/charWidth already resolved.

#pragma once

// How many lines FIT ENTIRELY in maxH at this line height.
//
// A line must fit WHOLE. The previous rule reserved a fixed 8*size pixels for the line's
// footprint, which was wrong in both directions: at the large rungs it under-reserved (a 10pt
// line was allowed to reach into the footer row) and at the smallest rung it over-reserved,
// dropping a line that plainly fitted — the panel came back as "space for another line" with
// 18 px free below the last one (LESSONS §68). The caller's maxH is the box: from the first
// line's top to the last line's bottom edge.
int cc_lineCapacity(int maxH, int lineHeight);

// Width budget for one line. When the text will be cut off, the final line is
// kept short enough that the "..." marker still fits inline: 6 glyph widths of
// slack for centred text (the line floats, so both margins count).
int cc_lineBudget(int maxW, int charWidth, bool truncated, int lineIdx, int capacity);
