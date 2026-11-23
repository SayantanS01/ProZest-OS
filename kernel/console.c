#include <stdint.h>
#include <stddef.h>

#include "console.h"
#include "graphics.h"
#include "font.h"

/* Console state. */
static struct limine_framebuffer *console_fb = NULL;

static uint64_t cursor_x = 0;
static uint64_t cursor_y = 0;

static uint64_t char_width = 0;
static uint64_t line_height = 0;

static uint32_t text_color = 0;
static uint32_t text_scale = 1;
static uint32_t background_color = 0;

/*
 * Initialize the graphical console.
 */
void console_init(
    struct limine_framebuffer *framebuffer,
    uint32_t foreground,
    uint32_t scale,
    uint32_t background
)
{
    console_fb = NULL;
    cursor_x = 0;
    cursor_y = 0;

    if (framebuffer == NULL || scale == 0) {
        return;
    }

    uint64_t width = (FONT_WIDTH + 1) * (uint64_t)scale;
    uint64_t height = (FONT_HEIGHT + 1) * (uint64_t)scale;

    if (width > framebuffer->width ||
        height > framebuffer->height) {
        return;
    }

    char_width = width;
    line_height = height;
    text_color = foreground;
    text_scale = scale;
    background_color = background;

    console_fb = framebuffer;
}

/*
 * Move to the beginning of the next line.
 *
 * Scrolling will be implemented later.
 */
void console_newline(void)
{
    if (console_fb == NULL) {
        return;
    }

    cursor_x = 0;

    /*
     * If another complete text row fits,
     * advance the cursor normally.
     */
    if (cursor_y <= console_fb->height - line_height &&
        line_height <=
            console_fb->height - line_height - cursor_y) {
        cursor_y += line_height;
        return;
    }

    /*
     * Otherwise, move the existing screen
     * contents upward by one text row.
     *
     * Keep cursor_y at the last usable line.
     */
    graphics_scroll_up(
        console_fb,
        line_height,
        background_color
    );
}

/*
 * Print text with newline and line wrapping.
 */
void console_write(const char *text)
{
    if (console_fb == NULL || text == NULL) {
        return;
    }

    while (*text != '\0') {
        char character = *text++;

        if (character == '\n') {
            console_newline();
            continue;
        }

        if (cursor_x >= console_fb->width ||
            char_width > console_fb->width - cursor_x) {
            console_newline();
        }

        if (cursor_y >= console_fb->height ||
            line_height > console_fb->height - cursor_y) {
            return;
        }

        graphics_draw_char(
            console_fb,
            character,
            cursor_x,
            cursor_y,
            text_color,
            text_scale
        );

        cursor_x += char_width;
    }
}
