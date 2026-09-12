// glyphs.cpp — the single UTF-8 -> ASCII decoder for CHROMAWOTD.
// See glyphs.h for the contract.

#include "glyphs.h"

int cc_utf8ToAscii(const unsigned char* p, unsigned char* out) {
    unsigned char c = p[0];
    if (c < 0x80) { *out = c; return 1; }

    // 2-byte sequences (U+0080..U+07FF)
    if (c == 0xC2 && p[1]) {
        if (p[1] == 0xB0) { *out = CC_DEGREE; return 2; }  // ° degree sign
        if (p[1] == 0xA0) { *out = ' ';       return 2; }  // non-breaking space
        *out = CC_UNKNOWN; return 2;
    }
    // 3-byte sequences (U+2000..U+2FFF)
    if (c == 0xE2 && p[1] && p[2]) {
        if (p[1] == 0x80) {
            switch (p[2]) {
                case 0x93: case 0x94: case 0x95: *out = '-';  return 3;  // – — ―
                case 0x98: case 0x99:            *out = '\''; return 3;  // ‘ ’
                case 0x9C: case 0x9D:            *out = '"';  return 3;  // “ ”
                case 0xA2:                       *out = '\''; return 3;  // ′ prime
                case 0xA6:                       *out = '.';  return 3;  // … ellipsis
                default: break;
            }
        }
        if (p[1] == 0x9A && p[2] == 0xA0) { *out = '!'; return 3; }  // ⚠ warning sign
        *out = CC_UNKNOWN; return 3;
    }
    // 4-byte sequences (emoji etc.) collapse to a single replacement glyph.
    if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { *out = CC_UNKNOWN; return 4; }
    *out = CC_UNKNOWN; return 1;
}

int cc_utf8ToAsciiN(const unsigned char* p, const unsigned char* end, unsigned char* out) {
    if (p >= end) { *out = CC_UNKNOWN; return 1; }  // defensive; callers loop on p < end
    unsigned char c = p[0];
    if (c < 0x80) { *out = c; return 1; }
    const int avail = (int)(end - p);

    // 2-byte sequences (U+0080..U+07FF)
    if (c == 0xC2) {
        if (avail < 2) { *out = CC_UNKNOWN; return 1; }  // truncated lead byte
        if (p[1] == 0xB0) { *out = CC_DEGREE; return 2; }  // ° degree sign
        if (p[1] == 0xA0) { *out = ' ';       return 2; }  // non-breaking space
        *out = CC_UNKNOWN; return 2;
    }
    // 3-byte sequences (U+2000..U+2FFF)
    if (c == 0xE2) {
        if (avail < 3) { *out = CC_UNKNOWN; return 1; }  // truncated lead byte
        if (p[1] == 0x80) {
            switch (p[2]) {
                case 0x93: case 0x94: case 0x95: *out = '-';  return 3;  // – — ―
                case 0x98: case 0x99:            *out = '\''; return 3;  // ‘ ’
                case 0x9C: case 0x9D:            *out = '"';  return 3;  // “ ”
                case 0xA2:                       *out = '\''; return 3;  // ′ prime
                case 0xA6:                       *out = '.';  return 3;  // … ellipsis
                default: break;
            }
        }
        if (p[1] == 0x9A && p[2] == 0xA0) { *out = '!'; return 3; }  // ⚠ warning sign
        *out = CC_UNKNOWN; return 3;
    }
    // 4-byte sequences (emoji etc.) collapse to a single replacement glyph.
    if ((c & 0xF8) == 0xF0) {
        if (avail < 4) { *out = CC_UNKNOWN; return 1; }  // truncated lead byte
        *out = CC_UNKNOWN; return 4;
    }
    *out = CC_UNKNOWN; return 1;
}
