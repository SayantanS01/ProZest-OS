#ifndef PROZEST_GRAPHICS_H
#define PROZEST_GRAPHICS_H

#include <stdint.h>
#include <limine.h>

uint32_t graphics_rgb(
    const struct limine_framebuffer *fb,
    uint8_t r, uint8_t g, uint8_t b
);

void graphics_draw_char(
    struct limine_framebuffer *fb,
    char character,
    uint64_t x,
    uint64_t y,
    uint32_t color,
    uint32_t scale
);

void graphics_draw_string(
    struct limine_framebuffer *fb,
    const char *text,
    uint64_t x,
    uint64_t y,
    uint32_t color,
    uint32_t scale
);


/* Move framebuffer pixels upward and clear the bottom area. */
void graphics_scroll_up(
    struct limine_framebuffer *fb,
    uint64_t pixel_rows,
    uint32_t background
);

#endif
