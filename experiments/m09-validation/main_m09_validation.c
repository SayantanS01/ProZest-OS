#include "qemu_debugcon.h"
/*
 * Original OS — First Framebuffer Experiment
 * Architecture: x86-64
 */

#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "graphics.h"
#include "console.h"
#include "logger.h"
#include "cpu_tables_probe.h"
#include "cpu_tables_validation_report.h"

/* Limine protocol markers and requests. */

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

/* Boot progress indicator. */
volatile uint64_t boot_stage = 0;

__attribute__((noreturn))
static void kernel_halt(void)
{
    for (;;) {
        __asm__ volatile ("pause");
    }
}

/* Convert RGB values into the framebuffer pixel format. */
static uint32_t make_color(
    struct limine_framebuffer *fb,
    uint8_t red,
    uint8_t green,
    uint8_t blue
)
{
    return ((uint32_t)red << fb->red_mask_shift) |
           ((uint32_t)green << fb->green_mask_shift) |
           ((uint32_t)blue << fb->blue_mask_shift);
}

/* Write one pixel into video memory. */
static void put_pixel(
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

/* Draw a simple four-color screen. */
static void draw_test_pattern(struct limine_framebuffer *fb)
{
    uint32_t blue  = make_color(fb, 22, 101, 193);
    uint32_t green = make_color(fb, 22, 160, 133);
    uint32_t gold  = make_color(fb, 212, 163, 63);
    uint32_t red   = make_color(fb, 217, 84, 102);

    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            uint32_t color;

            if (y < fb->height / 2) {
                color = (x < fb->width / 2)
                    ? blue : green;
            } else {
                color = (x < fb->width / 2)
                    ? gold : red;
            }

            put_pixel(fb, x, y, color);
        }
    }
}

/* Our original operating system's entry point. */
__attribute__((noreturn))
void kernel_main(void)
{
    boot_stage = 1;

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        kernel_halt();
    }

    boot_stage = 2;

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0 ||
        framebuffer_request.response->framebuffers == NULL ||
        framebuffer_request.response->framebuffers[0] == NULL) {
        kernel_halt();
    }

    struct limine_framebuffer *fb =
        framebuffer_request.response->framebuffers[0];

    /*
     * First driver supports 32-bit RGB framebuffers
     * with 8-bit red, green and blue channels.
     */
    if (fb->address == NULL ||
        fb->bpp != 32 ||
        fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->red_mask_size != 8 ||
        fb->green_mask_size != 8 ||
        fb->blue_mask_size != 8 ||
        fb->red_mask_shift > 24 ||
        fb->green_mask_shift > 24 ||
        fb->blue_mask_shift > 24 ||
        fb->width == 0 ||
        fb->height == 0 ||
        fb->width > UINT64_MAX / 4 ||
        fb->pitch < fb->width * 4) {
        kernel_halt();
    }

    boot_stage = 3;

    draw_test_pattern(fb);

    /* Initialize our first graphical kernel console. */
    uint32_t white = graphics_rgb(fb, 255, 255, 255);

    uint32_t black = graphics_rgb(fb, 0, 0, 0);

    console_init(fb, white, 3, black);

    console_write("PROZEST OS v0.0.6\n");
    console_write("=================\n");
    console_newline();

    log_write(LOG_INFO, "Kernel initialization started");
    log_write(LOG_OK, "Framebuffer available");
    log_write(LOG_OK, "Graphical console initialized");
    log_write(LOG_OK, "ASCII bitmap font available");
    log_write(LOG_DEBUG, "Logger subsystem active");

    console_newline();

    log_write(LOG_WARN, "TEST ONLY - example warning");
    log_write(LOG_ERROR, "TEST ONLY - example error");

    console_newline();

    log_write(LOG_INFO, "ProZest OS kernel ready");

    /*
     * M09 experimental build only.
     * Prepare CPU descriptor tables in memory.
     * Do not activate them on the processor.
     */
    log_write(LOG_INFO, "Preparing M09 CPU tables");

    if (cpu_tables_probe_run()) {
        qemu_debugcon_puts("CPU TABLE PROBE: PASS\n");
        log_write(LOG_OK, "M09 CPU table probe passed");

        /*
         * Validate the already-prepared tables.
         * No descriptor-register activation.
         */
        if (cpu_tables_validation_report_run()) {
            qemu_debugcon_puts("DESCRIPTOR VALIDATION: PASS\n");
            log_write(
                LOG_OK,
                "M09 descriptor validation passed"
            );
        } else {
            log_write(
                LOG_ERROR,
                "M09 descriptor validation failed"
            );
        }

    } else {
        log_write(LOG_ERROR, "M09 CPU table probe failed");
    }



    boot_stage = 4;

    kernel_halt();
}
