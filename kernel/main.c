/* BSP-only, controlled activation Limine entry point. */
#include <limine.h>
#include <stdint.h>
#include "graphics.h"
#include "console.h"
#include "logger.h"

#include "cpu_state.h"
#include "cpu_tables_preflight.h"
#include "debugcon_probe.h"
#include "descriptor_transition.h"
#include "exception_gate_identity.h"
#include "qemu_debugcon.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

#include "descriptor_activation.h"
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID
};

static const char *const field_name[] = {
    "GDTR_BASE", "GDTR_LIMIT", "IDTR_BASE", "IDTR_LIMIT", "CS", "SS", "DS",
    "ES", "FS", "GS", "TR", "FS_BASE", "GS_BASE", "KERNEL_GS_BASE", "CR3",
    "CR4", "IF"
};
_Static_assert(sizeof(field_name) / sizeof(field_name[0]) == FIELD_COUNT,
               "Every captured field needs a debugcon label");

static uint64_t read_msr(uint32_t index)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return ((uint64_t)high << 32) | low;
}

static void capture(struct handoff_state *state)
{
    struct cpu_state_snapshot cpu;
    uint16_t fs, gs;
    uint64_t cr3, cr4, flags;
    cpu_state_capture(&cpu);
    __asm__ volatile ("mov %%fs,%0" : "=r"(fs));
    __asm__ volatile ("mov %%gs,%0" : "=r"(gs));
    __asm__ volatile ("mov %%cr3,%0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4,%0" : "=r"(cr4));
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(flags) :: "memory");

    *state = (struct handoff_state){ .value = {
        [GDTR_BASE] = cpu.gdtr.base, [GDTR_LIMIT] = cpu.gdtr.limit,
        [IDTR_BASE] = cpu.idtr.base, [IDTR_LIMIT] = cpu.idtr.limit,
        [CS] = cpu.cs, [SS] = cpu.ss, [DS] = cpu.ds, [ES] = cpu.es,
        [FS] = fs, [GS] = gs, [TR] = cpu.tr,
        [FS_BASE] = read_msr(0xc0000100), [GS_BASE] = read_msr(0xc0000101),
        [KERNEL_GS_BASE] = read_msr(0xc0000102),
        [CR3] = cr3, [CR4] = cr4, [INTERRUPT_FLAG] = (flags >> 9) & 1
    }};
}

static void print_state(const char *phase, const struct handoff_state *state)
{
    for (unsigned int i = 0; i < FIELD_COUNT; ++i) {
        qemu_debugcon_puts(phase);
        qemu_debugcon_putc(' ');
        qemu_debugcon_puts(field_name[i]);
        qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(state->value[i]);
        qemu_debugcon_putc('\n');
    }
}

/* Query live descriptor access rights without changing any selector.
 * Ignore the accessed bit. For 64-bit code also require L=1 and D=0. */
static int rights_match(uint16_t selector, uint32_t mask, uint32_t expected)
{
    uint32_t rights;
    unsigned char valid;
    __asm__ volatile ("lar %w2,%0" : "=r"(rights), "=@ccz"(valid)
                      : "r"(selector));
    return valid && (rights & mask) == expected;
}

__attribute__((noinline, noreturn))
void transition_checkpoint_halt(void)
{
    for (;;) {
        __asm__ volatile ("cli\n\thlt" ::: "memory");
    }
}

static void check(const char *name, int passed)
{
    qemu_debugcon_puts(name);
    qemu_debugcon_puts(passed ? "\n" : ": FAIL\n");
    if (!passed)
        transition_checkpoint_halt();
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

static void initialize_display(void)
{
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0 ||
        framebuffer_request.response->framebuffers == NULL ||
        framebuffer_request.response->framebuffers[0] == NULL) {
        check("FRAMEBUFFER", 0);
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
        fb->width < 640 ||
        fb->height < 480 ||
        fb->width > UINT64_MAX / 4 ||
        fb->pitch < fb->width * 4) {
        check("FRAMEBUFFER", 0);
    }



    draw_test_pattern(fb);

    /* Initialize our first graphical kernel console. */
    uint32_t white = graphics_rgb(fb, 255, 255, 255);

    uint32_t black = graphics_rgb(fb, 0, 0, 0);

    console_init(fb, white, 2, black);

    console_write("PROZEST OS v0.0.7-rc1\n\n");
    log_write(LOG_OK, "Framebuffer and graphical console ready");
}

__attribute__((section(".text.kernel_entry"), noreturn))
void kernel_main(void)
{
    struct handoff_state before, after;
    debugcon_probe();
    capture(&before);

    check("HANDOFF VERIFIED", LIMINE_BASE_REVISION_SUPPORTED(revision) &&
                     supported_handoff(&before) &&
                     rights_match(before.value[CS], 0x60fe00, 0x209a00) &&
                     rights_match(before.value[DS], 0xfe00, 0x9200));

    const struct bootstrap_idt_state *early = descriptor_transition_prepare_early_idt();
    if (!early)
        check("EARLY IDT PREPARATION", 0);
    __asm__ volatile ("lidt %0" :: "m"(early->idtr) : "memory");
    qemu_debugcon_puts("EARLY IDT ACTIVE\n");
    initialize_display();
    log_write(LOG_OK, "CPU handoff verified");
    print_state("BEFORE", &before);

    /* A second entry must never reinitialize potentially live storage. */
    int fresh = !bootstrap_cpu_tables_get() && !bootstrap_idt_get();
    int ready = fresh && bootstrap_cpu_tables_prepare() && bootstrap_idt_prepare();
    const struct bootstrap_cpu_tables *cpu = bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *irq = bootstrap_idt_get();
    check("FINAL TABLES", ready && cpu_tables_preflight_check(cpu, irq) &&
                          exception_gate_identity_check(&irq->table));
    check("TRANSITION TABLES", descriptor_transition_prepare() &&
          descriptor_transition_validate(descriptor_transition_get(), cpu, irq));

    log_write(LOG_OK, "CPU descriptor tables prepared and validated");
    const struct descriptor_transition *bridge = descriptor_transition_get();
    controlled_install(&bridge->gdtr, &bridge->target_idt.idtr, &cpu->gdtr, &irq->idtr);
    capture(&after);
    print_state("AFTER", &after);
    qemu_debugcon_puts("TSS ACCESS ");
    qemu_debugcon_hex64(cpu->gdt.tss_descriptor.access);
    qemu_debugcon_putc('\n');
    check("CPU STATE VERIFIED", active_state(&before, &after, cpu, irq) &&
          cpu_tables_active_check(cpu, irq) && exception_gate_identity_check(&irq->table));
    qemu_debugcon_puts("DESCRIPTOR ACTIVATION: PASS\n");
    log_write(LOG_OK, "Kernel GDT, TSS and IDT active");
    log_write(LOG_OK, "Fatal exception reporting ready");
    log_write(LOG_INFO, "Hardware interrupts disabled");
    log_write(LOG_INFO, "Physical memory manager not initialized");
    console_newline();
    console_write("KERNEL READY - M09\n");
    qemu_debugcon_puts("KERNEL READY - M09\n");
    transition_checkpoint_halt();
}
