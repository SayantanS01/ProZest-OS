#include "font.h"

static const uint8_t glyph_p[7] = {
    0b11110, 0b10001, 0b10001, 0b11110,
    0b10000, 0b10000, 0b10000
};

static const uint8_t glyph_r[7] = {
    0b11110, 0b10001, 0b10001, 0b11110,
    0b10100, 0b10010, 0b10001
};

static const uint8_t glyph_o[7] = {
    0b01110, 0b10001, 0b10001, 0b10001,
    0b10001, 0b10001, 0b01110
};

static const uint8_t glyph_z[7] = {
    0b11111, 0b00001, 0b00010, 0b00100,
    0b01000, 0b10000, 0b11111
};

static const uint8_t glyph_e[7] = {
    0b11111, 0b10000, 0b10000, 0b11110,
    0b10000, 0b10000, 0b11111
};

static const uint8_t glyph_s[7] = {
    0b01111, 0b10000, 0b10000, 0b01110,
    0b00001, 0b00001, 0b11110
};

static const uint8_t glyph_t[7] = {
    0b11111, 0b00100, 0b00100, 0b00100,
    0b00100, 0b00100, 0b00100
};

static const uint8_t glyph_space[7] = {
    0, 0, 0, 0, 0, 0, 0
};

static const uint8_t glyph_unknown[7] = {
    0b01110, 0b10001, 0b00001, 0b00110,
    0b00100, 0b00000, 0b00100
};

const uint8_t *font_get_glyph(char character)
{
    switch (character) {
        case 'P': return glyph_p;
        case 'R': return glyph_r;
        case 'O': return glyph_o;
        case 'Z': return glyph_z;
        case 'E': return glyph_e;
        case 'S': return glyph_s;
        case 'T': return glyph_t;
        case ' ': return glyph_space;
        default:  return glyph_unknown;
    }
}
