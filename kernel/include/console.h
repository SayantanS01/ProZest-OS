#ifndef PROZEST_CONSOLE_H
#define PROZEST_CONSOLE_H

#include <stdint.h>
#include <limine.h>

/*
 * Initialize the graphical kernel console.
 *
 * scale controls the size of bitmap characters.
 * foreground is the pixel color used for text.
 */
void console_init(
    struct limine_framebuffer *framebuffer,
    uint32_t foreground,
    uint32_t scale,
    uint32_t background
);

/* Write a null-terminated string to the console. */
void console_write(const char *text);

/* Move to the beginning of the next line. */
void console_newline(void);

#endif
