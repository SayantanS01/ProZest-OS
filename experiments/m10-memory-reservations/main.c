/* BSP-only, controlled activation Limine entry point. */
#include <limine.h>
#include <stdint.h>
#include "graphics.h"
#include "console.h"
#include "logger.h"
#include "memory_map.h"
#include "memory_reservations.h"
#include "page_table_inventory.h"
#include <cpuid.h>

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
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request map_request = {.id=LIMINE_MEMMAP_REQUEST_ID};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {.id=LIMINE_HHDM_REQUEST_ID};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_request = {
    .id=LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID
};

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

static struct memory_inventory inventory;
extern char __kernel_end[];
static void number(const char *label, uint64_t value)
{
    qemu_debugcon_puts(label);
    qemu_debugcon_hex64(value);
    qemu_debugcon_putc('\n');
}
static void reservation(const char *name, uint64_t start, uint64_t end,
                        const struct limine_executable_address_response *exe)
{
    qemu_debugcon_puts("RESERVATION "); qemu_debugcon_puts(name);
    qemu_debugcon_puts(" virtual "); qemu_debugcon_hex64(start);
    qemu_debugcon_putc(' '); qemu_debugcon_hex64(end);
    if (exe && start >= exe->virtual_base && end >= start &&
        end <= (uint64_t)(uintptr_t)__kernel_end &&
        exe->physical_base <= UINT64_MAX - (end - exe->virtual_base)) {
        qemu_debugcon_puts(" physical ");
        qemu_debugcon_hex64(exe->physical_base + start - exe->virtual_base);
        qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(exe->physical_base + end - exe->virtual_base);
    } else qemu_debugcon_puts(" physical UNRESOLVED");
    qemu_debugcon_putc('\n');
}
static void reservations(void)
{
    const struct limine_executable_address_response *exe = executable_request.response;
    const struct bootstrap_cpu_tables *cpu = bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *idt = bootstrap_idt_get();
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp,%0" : "=r"(rsp));
    if (exe && (uint64_t)(uintptr_t)__kernel_end >= exe->virtual_base) {
        reservation("kernel ELF load image including BSS", exe->virtual_base,
                    (uint64_t)(uintptr_t)__kernel_end, exe);
    } else qemu_debugcon_puts("RESERVATION kernel ELF physical UNRESOLVED\n");
    reservation("active GDT TSS", (uint64_t)(uintptr_t)cpu,
                (uint64_t)(uintptr_t)cpu + sizeof(*cpu), exe);
    reservation("active IDT", (uint64_t)(uintptr_t)idt,
                (uint64_t)(uintptr_t)idt + sizeof(*idt), exe);
    for (unsigned i=0; i<3; ++i) {
        uint64_t top = cpu->tss.ist[i];
        reservation(i == 0 ? "IST1 double fault" : i == 1 ? "IST2 NMI" : "IST3 machine check",
                    top - TSS_IST_STACK_SIZE, top, exe);
    }
    number("RESERVATION current RSP only, stack bounds UNRESOLVED: ", rsp);
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3,%0" : "=r"(cr3));
    number("RESERVATION active CR3 root physical, tree bounds UNRESOLVED: ", cr3 & ~UINT64_C(4095));
    number("RESERVATION Limine map response virtual: ", (uint64_t)(uintptr_t)map_request.response);
    number("RESERVATION Limine HHDM response virtual: ", (uint64_t)(uintptr_t)hhdm_request.response);
    number("RESERVATION framebuffer response virtual: ", (uint64_t)(uintptr_t)framebuffer_request.response);
    number("RESERVATION framebuffer address virtual: ",
           (uint64_t)(uintptr_t)framebuffer_request.response->framebuffers[0]->address);
    qemu_debugcon_puts("RESERVATION bootloader reclaimable, ACPI, firmware, framebuffer and unknown regions unavailable\n");
    qemu_debugcon_puts("RESERVATION Limine response backing and full page-table tree physical bounds UNRESOLVED\n");
}

static struct reservation_inventory protected;
static struct page_table_inventory tables;
static uint64_t image_physical;
static int owned(uint64_t base, uint64_t end, uint64_t type)
{
    if (base >= end) return 0;
    for (uint64_t i=0;i<inventory.count;i++)
        if (inventory.region[i].type==type && inventory.region[i].base<=base &&
            end<=inventory.region[i].end) return 1;
    return 0;
}
static int hhdm_owned(const void *pointer, uint64_t length, uint64_t type)
{
    uint64_t v=(uint64_t)(uintptr_t)pointer;
    if (!pointer || v<inventory.hhdm || length>UINT64_MAX-(v-inventory.hhdm)) return 0;
    return owned(v-inventory.hhdm,v-inventory.hhdm+length,type);
}
static int kernel_object(const void *pointer, uint64_t size, uint64_t image_end)
{
    uint64_t v=(uint64_t)(uintptr_t)pointer;
    uint64_t start=executable_request.response->virtual_base;
    return v>=start && v<=image_end && size<=image_end-v &&
           owned(image_physical+v-start,image_physical+v-start+size,
                 LIMINE_MEMMAP_EXECUTABLE_AND_MODULES);
}
static int reserve_kernel_object(const void *pointer, uint64_t size,
                                 uint64_t image_end, uint32_t reason, unsigned bits)
{
    if (!kernel_object(pointer,size,image_end)) return 0;
    uint64_t v=(uint64_t)(uintptr_t)pointer;
    uint64_t p=image_physical+v-executable_request.response->virtual_base;
    return reservation_add(&protected,p,p+size,reason,bits);
}
static int reserve_hhdm_object(const void *pointer, uint64_t size,
                               uint32_t reason, unsigned bits)
{
    if (!hhdm_owned(pointer,size,LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)) return 0;
    uint64_t p=(uint64_t)(uintptr_t)pointer-inventory.hhdm;
    return reservation_add(&protected,p,p+size,reason,bits);
}
static void reservation_run(void)
{
    unsigned eax=0,ebx=0,ecx=0,edx=0;
    check("PHYSICAL WIDTH RECHECK: PASS",__get_cpuid(0x80000008,&eax,&ebx,&ecx,&edx));
    unsigned bits=eax&255;
    const struct limine_executable_address_response *exe=executable_request.response;
    uint64_t image_end=(uint64_t)(uintptr_t)__kernel_end;
    check("KERNEL RESERVATION: PASS", exe && exe->virtual_base<=image_end &&
          exe->physical_base < (UINT64_C(1)<<bits) &&
          image_end-exe->virtual_base < UINT64_MAX-exe->physical_base);
    image_physical=exe->physical_base;
    check("KERNEL IMAGE OWNERSHIP: PASS",
          owned(image_physical,image_physical+image_end-exe->virtual_base,
                LIMINE_MEMMAP_EXECUTABLE_AND_MODULES));
    check("RESERVATION PAGE ZERO: PASS",reservation_add(&protected,0,4096,RES_PAGE_ZERO,bits));
    for (uint64_t i=0;i<inventory.count;i++) if (inventory.region[i].type!=LIMINE_MEMMAP_USABLE)
        check("NONUSABLE REGION RESERVED",reservation_add(&protected,inventory.region[i].base,
              inventory.region[i].end,RES_NONUSABLE,bits));
    check("KERNEL IMAGE RANGE: PASS",reservation_add(&protected,image_physical,
          image_physical+image_end-exe->virtual_base,RES_KERNEL,bits));
    const struct bootstrap_cpu_tables *cpu=bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *idt=bootstrap_idt_get();
    check("DESCRIPTOR RESERVATIONS: PASS",
          reserve_kernel_object(cpu,sizeof(*cpu),image_end,RES_DESCRIPTOR,bits) &&
          reserve_kernel_object(idt,sizeof(*idt),image_end,RES_DESCRIPTOR,bits));
    int stacks=1;
    for (unsigned i=0;i<3;i++) {
        uint64_t top=cpu->tss.ist[i];
        stacks &= top>=TSS_IST_STACK_SIZE &&
            reserve_kernel_object((void *)(uintptr_t)(top-TSS_IST_STACK_SIZE),
                                  TSS_IST_STACK_SIZE,image_end,RES_IST,bits);
    }
    check("IST STACK RESERVATIONS: PASS",stacks);
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp,%0":"=r"(rsp));
    check("NORMAL STACK PROTECTION: PASS",hhdm_owned((void *)(uintptr_t)(rsp-1),1,
          LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE));
    uint64_t stack_physical=rsp-1-inventory.hhdm;
    for (uint64_t i=0;i<inventory.count;i++)
        if (inventory.region[i].type==LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
            inventory.region[i].base<=stack_physical && stack_physical<inventory.region[i].end)
            check("NORMAL STACK REGION RESERVED",reservation_add(&protected,
                  inventory.region[i].base,inventory.region[i].end,RES_STACK,bits));
    number("NORMAL STACK RSP virtual: ",rsp);
    qemu_debugcon_puts("NORMAL STACK BOUNDS: unresolved; entire bootloader-reclaimable region excluded\n");
    uint64_t cr3,cr0,cr4,efer_low,efer_high;
    __asm__ volatile ("mov %%cr3,%0":"=r"(cr3));
    __asm__ volatile ("mov %%cr0,%0":"=r"(cr0));
    __asm__ volatile ("mov %%cr4,%0":"=r"(cr4));
    uint32_t lo,hi;
    __asm__ volatile ("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000080));
    efer_low=lo; efer_high=hi;
    struct hhdm_table_context ctx={&inventory,inventory.hhdm};
    check("ACTIVE PAGE TABLE INVENTORY: PASS",
          page_tables_walk(&tables,cr3,cr0,cr4,efer_low|(efer_high<<32),bits,
                           page_table_hhdm_resolve,&ctx));
    number("PAGE TABLE FRAMES PML4: ",tables.by_level[4]);
    number("PAGE TABLE FRAMES PDPT: ",tables.by_level[3]);
    number("PAGE TABLE FRAMES PD: ",tables.by_level[2]);
    number("PAGE TABLE FRAMES PT: ",tables.by_level[1]);
    number("PAGE TABLE 1G LEAVES: ",tables.large_1g);
    number("PAGE TABLE 2M LEAVES: ",tables.large_2m);
    for (uint32_t i=0;i<tables.count;i++) {
        uint64_t p=tables.frame[i].physical;
        check("PAGE TABLE FRAME RESERVED",reservation_add(&protected,p,p+4096,RES_PAGE_TABLE,bits));
        qemu_debugcon_puts("TABLE "); qemu_debugcon_hex64(p); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(tables.frame[i].level); qemu_debugcon_putc('\n');
    }
    check("BOOTLOADER MEMORY PROTECTION: PASS",
          hhdm_owned((const void *)map_request.response,sizeof(*map_request.response),5) &&
          hhdm_owned((const void *)hhdm_request.response,sizeof(*hhdm_request.response),5) &&
          hhdm_owned((const void *)exe,sizeof(*exe),5) &&
          hhdm_owned((const void *)map_request.response->entries,
                     map_request.response->entry_count*sizeof(void *),5) &&
          hhdm_owned((const void *)framebuffer_request.response,
                     sizeof(*framebuffer_request.response),5) &&
          hhdm_owned((const void *)framebuffer_request.response->framebuffers,
                     framebuffer_request.response->framebuffer_count*sizeof(void *),5));
    check("RESPONSE RANGES RESERVED",
          reserve_hhdm_object((const void *)map_request.response,sizeof(*map_request.response),RES_RESPONSE,bits) &&
          reserve_hhdm_object((const void *)hhdm_request.response,sizeof(*hhdm_request.response),RES_RESPONSE,bits) &&
          reserve_hhdm_object((const void *)exe,sizeof(*exe),RES_RESPONSE,bits) &&
          reserve_hhdm_object((const void *)map_request.response->entries,
                map_request.response->entry_count*sizeof(void *),RES_RESPONSE,bits) &&
          reserve_hhdm_object((const void *)framebuffer_request.response,
                sizeof(*framebuffer_request.response),RES_RESPONSE,bits) &&
          reserve_hhdm_object((const void *)framebuffer_request.response->framebuffers,
                framebuffer_request.response->framebuffer_count*sizeof(void *),RES_RESPONSE,bits));
    for (uint64_t i=0;i<map_request.response->entry_count;i++)
        check("MEMORY RESPONSE ENTRY PROTECTED",reserve_hhdm_object(
              (const void *)map_request.response->entries[i],
              sizeof(struct limine_memmap_entry),RES_RESPONSE,bits));
    struct limine_framebuffer *fb=framebuffer_request.response->framebuffers[0];
    check("FRAMEBUFFER RESPONSE PROTECTED",reserve_hhdm_object(fb,sizeof(*fb),RES_RESPONSE,bits));
    uint64_t fbv=(uint64_t)(uintptr_t)fb->address;
    check("FRAMEBUFFER RESERVATION: PASS",fb->height && fb->pitch<=UINT64_MAX/fb->height &&
          fbv>=inventory.hhdm &&
          fb->pitch*fb->height<=UINT64_MAX-(fbv-inventory.hhdm) &&
          owned(fbv-inventory.hhdm,fbv-inventory.hhdm+fb->pitch*fb->height,
                LIMINE_MEMMAP_FRAMEBUFFER));
    check("FRAMEBUFFER RANGE: PASS",reservation_add(&protected,fbv-inventory.hhdm,
          fbv-inventory.hhdm+fb->pitch*fb->height,RES_FRAMEBUFFER,bits));
    check("RESERVATION ENGINE STORAGE: PASS",
          reserve_kernel_object(&protected,sizeof(protected),image_end,RES_ENGINE,bits) &&
          reserve_kernel_object(&tables,sizeof(tables),image_end,RES_ENGINE,bits));
    check("RESERVATION NORMALIZATION: PASS",reservations_build(&protected,&inventory));
    number("Original usable page candidates: ",inventory.candidates);
    number("Unique candidate pages excluded: ",protected.excluded_pages);
    number("Allocator-eligible pages: ",protected.eligible_pages);
    number("Allocator-eligible bytes: ",protected.eligible_bytes);
    number("Eligible range count: ",protected.eligible_count);
    number("Largest eligible range bytes: ",protected.largest_bytes);
    for (uint32_t i=0;i<protected.item_count;i++) {
        const struct reservation_item *p=&protected.item[i];
        qemu_debugcon_puts("PROTECTED "); qemu_debugcon_hex64(p->range.base);
        qemu_debugcon_putc(' '); qemu_debugcon_hex64(p->range.end);
        qemu_debugcon_putc(' '); qemu_debugcon_hex64(p->reason); qemu_debugcon_putc('\n');
    }
    for (uint32_t i=0;i<protected.eligible_count;i++) {
        qemu_debugcon_puts("ELIGIBLE "); qemu_debugcon_hex64(protected.eligible[i].base);
        qemu_debugcon_putc(' '); qemu_debugcon_hex64(protected.eligible[i].end);
        qemu_debugcon_putc('\n');
    }
    check("ELIGIBLE RANGE VALIDATION: PASS",protected.eligible_pages<=inventory.candidates);
    console_write("M10.2 eligible pages: 0x");
    char digits[17];
    for (unsigned i=0;i<16;i++) digits[i]="0123456789ABCDEF"[(protected.eligible_pages>>(60-i*4))&15];
    digits[16]=0; console_write(digits); console_write("\n");
    qemu_debugcon_puts("ALLOCATOR-SAFE INVENTORY: PASS\n");
    qemu_debugcon_puts("PHYSICAL MEMORY ALLOCATOR: NOT INITIALIZED\n");
    qemu_debugcon_puts("M10.2 COMPLETE\n");
}
static void inventory_run(void)
{
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    check("LIMINE MEMORY MAP: FOUND", map_request.response != NULL);
    check("HHDM RESPONSE: FOUND", hhdm_request.response != NULL);
    check("EXECUTABLE ADDRESS: FOUND", executable_request.response != NULL);
    check("PHYSICAL WIDTH: PASS", __get_cpuid(0x80000008, &eax,&ebx,&ecx,&edx));
    check("MEMORY MAP VALIDATION: PASS",
          memory_map_build(map_request.response, hhdm_request.response, eax & 255, &inventory));
    qemu_debugcon_puts("PROZEST OS - M10 MEMORY INVENTORY\n");
    number("Physical address bits: ", eax & 255);
    number("Memory map entries: ", inventory.count);
    number("Total described memory: ", inventory.described);
    number("Highest described physical address: ", inventory.highest);
    number("Raw usable memory: ", inventory.usable);
    number("Reserved and unavailable memory: ", inventory.described - inventory.usable);
    number("Bootloader-reclaimable memory: ", inventory.category[LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE]);
    number("Kernel and module memory: ", inventory.category[LIMINE_MEMMAP_EXECUTABLE_AND_MODULES]);
    number("Framebuffer memory: ", inventory.category[LIMINE_MEMMAP_FRAMEBUFFER]);
    number("Usable 4 KiB page candidates: ", inventory.candidates);
    number("HHDM offset: ", inventory.hhdm);
    for (unsigned i=0; i<10; ++i) {
        qemu_debugcon_puts("CATEGORY ");
        qemu_debugcon_puts(memory_type_name(i));
        qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(inventory.category[i]);
        qemu_debugcon_putc('\n');
    }
    for (unsigned i=0; i<inventory.count; ++i) {
        const struct memory_region *r = &inventory.region[i];
        qemu_debugcon_puts("REGION ");
        qemu_debugcon_hex64(i); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->base); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->end); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->type); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->pages); qemu_debugcon_putc('\n');
    }
    qemu_debugcon_puts("MEMORY CLASSIFICATION: PASS\n");
    reservations();
    qemu_debugcon_puts("PRELIMINARY RESERVATION INVENTORY: COMPLETE\n");
    qemu_debugcon_puts("PHYSICAL MEMORY ALLOCATOR: NOT INITIALIZED\n");
    console_write("M10 MEMORY INVENTORY\n");
    console_write("Physical memory map validated\n");
    console_write("Map entries: ");
    char digits[17];
    for (unsigned i=0; i<16; ++i) {
        unsigned nibble = (unsigned)((inventory.count >> (60 - i*4)) & 15);
        digits[i] = "0123456789ABCDEF"[nibble];
    }
    digits[16] = 0;
    console_write("0x"); console_write(digits); console_write("\n");
    console_write("Usable bytes: 0x");
    for (unsigned i=0; i<16; ++i) digits[i] = "0123456789ABCDEF"[(inventory.usable >> (60-i*4)) & 15];
    console_write(digits); console_write("\n");
    console_write("Usable page candidates: 0x");
    for (unsigned i=0; i<16; ++i) digits[i] = "0123456789ABCDEF"[(inventory.candidates >> (60-i*4)) & 15];
    console_write(digits); console_write("\n");
    console_write("Allocator not initialized\n");
    qemu_debugcon_puts("MEMORY INVENTORY: PASS\n");
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
    inventory_run();
    reservation_run();
    transition_checkpoint_halt();
}
