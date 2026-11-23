#ifndef ORIGINAL_OS_FONT_H
#define ORIGINAL_OS_FONT_H

#include <stdint.h>

/* Each character is five pixels wide and seven high. */
#define FONT_WIDTH  5
#define FONT_HEIGHT 7

/*
 * Return the seven bitmap rows for one character.
 * Each row uses its lowest five bits.
 */
const uint8_t *font_get_glyph(char character);

#endif
