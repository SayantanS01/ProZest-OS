#include <stdint.h>
#include <stddef.h>

#include "graphics.h"
#include "font.h"

/* Convert RGB components into framebuffer color format. */
uint32_t graphics_rgb(
    const struct limine_framebuffer *fb,
    uint8_t r, uint8_t g, uint8_t b
)
{
    return ((uint32_t)r << fb->red_mask_shift) |
           ((uint32_t)g << fb->green_mask_shift) |
           ((uint32_t)b << fb->blue_mask_shift);
}

/* Write one pixel, respecting framebuffer bounds. */
static void graphics_put_pixel(
    struct limine_framebuffer *fb,
    uint64_t x,
    uint64_t y,
    uint32_t color
)
{
    if (x >= fb->width || y >= fb->height) {
        return;
    }

    volatile uint32_t *pixel =
        (volatile uint32_t *)(
            (uintptr_t)fb->address +
            y * fb->pitch +
            x * 4
        );

    *pixel = color;
}

/* Render a scaled bitmap character. */
void graphics_draw_char(
    struct limine_framebuffer *fb,
    char character,
    uint64_t x,
    uint64_t y,
    uint32_t color,
    uint32_t scale
)
{
    const uint8_t *glyph = font_get_glyph(character);

    if (scale == 0 || x >= fb->width || y >= fb->height) {
        return;
    }

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {

            uint8_t bit = (glyph[row] >> (FONT_WIDTH - 1 - col)) & 1;

            if (!bit) {
                continue;
            }

            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {

                    uint64_t px = x + (uint64_t)col * scale + sx;
                    uint64_t py = y + (uint64_t)row * scale + sy;

                    graphics_put_pixel(fb, px, py, color);
                }
            }
        }
    }
}

/* Draw a sequence of characters. */
void graphics_draw_string(
    struct limine_framebuffer *fb,
    const char *text,
    uint64_t x,
    uint64_t y,
    uint32_t color,
    uint32_t scale
)
{
    if (text == NULL || scale == 0) {
        return;
    }

    uint64_t cursor_x = x;
    uint64_t advance = (FONT_WIDTH + 1) * (uint64_t)scale;

    while (*text != '\0') {
        if (cursor_x >= fb->width ||
            advance > fb->width - cursor_x) {
            break;
        }

        graphics_draw_char(
            fb,
            *text,
            cursor_x,
            y,
            color,
            scale
        );

        cursor_x += advance;
        text++;
    }
}

/*
 * Scroll the 32-bit framebuffer upward.
 *
 * Move existing pixel rows upward and clear
 * the exposed bottom rows.
 */
void graphics_scroll_up(
    struct limine_framebuffer *fb,
    uint64_t pixel_rows,
    uint32_t background
)
{
    if (fb == NULL || fb->address == NULL ||
        fb->bpp != 32 || fb->width > UINT64_MAX / 4 ||
        fb->pitch < fb->width * 4) {
        return;
    }

    if (pixel_rows == 0) {
        return;
    }

    if (pixel_rows > fb->height) {
        pixel_rows = fb->height;
    }

    /* Copy surviving rows upward. */
    for (uint64_t y = 0; y < fb->height - pixel_rows; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            volatile uint32_t *destination =
                (volatile uint32_t *)(
                    (uintptr_t)fb->address +
                    y * fb->pitch + x * 4
                );

            volatile uint32_t *source =
                (volatile uint32_t *)(
                    (uintptr_t)fb->address +
                    (y + pixel_rows) * fb->pitch + x * 4
                );

            *destination = *source;
        }
    }

    /* Clear the newly exposed bottom region. */
    for (uint64_t y = fb->height - pixel_rows;
         y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            graphics_put_pixel(fb, x, y, background);
        }
    }
}
