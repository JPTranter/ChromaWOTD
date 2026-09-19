// glyphs.h — UTF-8 -> ASCII decoding for ChromaWOTD.
//
// TFT_eSPI's built-in font is ASCII/CP437 and must never receive multi-byte
// UTF-8: it would draw one garbage glyph per byte. Real API text (BibleGateway,
// Open-Meteo) carries curly quotes, en/em dashes, non-breaking spaces, ellipsis
// and warning signs, so every draw path funnels through cc_utf8ToAscii().
// 0xB0 is the degree sign sentinel and is drawn as a vector circle, not a glyph.
//
// This is the project's single UTF-8 decoder; it is pure (no font/backend state)
// so it can be unit-tested in isolation.

#pragma once
#include <cstdint>

// Decoding sentinels, shared by the decoder and the layout engine.
#define CC_DEGREE  0xB0   // degree sign, drawn as a vector circle
#define CC_UNKNOWN '?'    // replacement for undecodable/unknown glyphs
#define CC_GLYPH_W 6      // built-in 5x7 font advance in px per size unit

// Decode the UTF-8 glyph starting at p to one ASCII byte.
// NUL-terminated variant: relies on the string's terminator to stop reads, so it
// is only safe for full C strings. Bounded callers must use cc_utf8ToAsciiN.
// Returns bytes consumed (>= 1).
int cc_utf8ToAscii(const unsigned char* p, unsigned char* out);

// Length-bounded variant: reads at most (end - p) bytes, never past `end`. A
// multi-byte sequence cut short by `end` collapses to a single CC_UNKNOWN
// replacement and consumes only its lead byte (>= 1), so the decoder can never
// read out of bounds regardless of input.
int cc_utf8ToAsciiN(const unsigned char* p, const unsigned char* end, unsigned char* out);
