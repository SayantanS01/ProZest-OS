#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "graphics.h"

#define WIDTH 4
#define HEIGHT 5
#define PITCH_WORDS 6
#define GUARD_WORDS 4

#define SENTINEL 0xA5A5A5A5u
#define BACKGROUND 0x00112233u

static int failures = 0;

static uint32_t original_pixel(
    uint32_t row,
    uint32_t col
)
{
    return 100u * row + col + 1u;
}

static void run_test(
    const char *name,
    uint64_t scroll_rows
)
{
    uint32_t memory[
        GUARD_WORDS +
        HEIGHT * PITCH_WORDS +
        GUARD_WORDS
    ];

    const size_t total_words =
        sizeof(memory) / sizeof(memory[0]);

    for (size_t i = 0; i < total_words; i++) {
        memory[i] = SENTINEL;
    }

    uint32_t *pixels = &memory[GUARD_WORDS];

    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            pixels[y * PITCH_WORDS + x] =
                original_pixel(y, x);
        }
    }

    struct limine_framebuffer fb = {0};

    fb.address = pixels;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.pitch = PITCH_WORDS * sizeof(uint32_t);
    fb.bpp = 32;

    graphics_scroll_up(
        &fb,
        scroll_rows,
        BACKGROUND
    );

    uint64_t effective_rows = scroll_rows;

    if (effective_rows > HEIGHT) {
        effective_rows = HEIGHT;
    }

    int passed = 1;

    /* Verify every visible pixel. */
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint32_t expected;

            if (y < HEIGHT - effective_rows) {
                expected = original_pixel(
                    y + (uint32_t)effective_rows,
                    x
                );
            } else {
                expected = BACKGROUND;
            }

            uint32_t actual =
                pixels[y * PITCH_WORDS + x];

            if (actual != expected) {
                printf(
                    "  Pixel (%u,%u): expected %08x, got %08x\n",
                    x, y, expected, actual
                );
                passed = 0;
            }
        }

        /* Padding must remain unchanged. */
        for (uint32_t x = WIDTH;
             x < PITCH_WORDS; x++) {

            if (pixels[y * PITCH_WORDS + x] !=
                SENTINEL) {
                printf("  Row %u padding modified\n", y);
                passed = 0;
            }
        }
    }

    /* Check memory before and after the framebuffer. */
    for (size_t i = 0; i < GUARD_WORDS; i++) {
        if (memory[i] != SENTINEL ||
            memory[GUARD_WORDS +
                   HEIGHT * PITCH_WORDS + i]
                   != SENTINEL) {
            printf("  Guard memory modified\n");
            passed = 0;
        }
    }

    printf("[%s] %s\n",
           passed ? "PASS" : "FAIL",
           name);

    if (!passed) {
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - Framebuffer Scroll Tests\n");
    printf("=====================================\n");

    run_test("Zero-row scroll", 0);
    run_test("One-row scroll", 1);
    run_test("Two-row scroll", 2);
    run_test("Full-height scroll", HEIGHT);
    run_test("Oversized scroll", HEIGHT + 10);

    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
