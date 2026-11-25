/* ProZest OS M13-M15 staged kernel. */
#if PROZEST_STAGE == 13
#define SERVICES_RELEASE "v0.0.7-rc5"
#define SERVICES_MILESTONE "M13"
#elif PROZEST_STAGE == 14
#define SERVICES_RELEASE "v0.0.7-rc6"
#define SERVICES_MILESTONE "M14"
#elif PROZEST_STAGE == 15
#define SERVICES_RELEASE "v0.0.7-rc7"
#define SERVICES_MILESTONE "M15"
#else
#error Unsupported stage
#endif
#include <limine.h>
#include <stdint.h>
#include "graphics.h"
#include "console.h"
#include "logger.h"
#include "memory_map.h"
#include "memory_reservations.h"
#include "page_table_inventory.h"
#include "pmm_plan.h"
#include "pmm_mapping.h"
#include "pmm.h"
#include "pmm_alloc.h"
#include "pmm_free.h"
#include "pmm_contiguous.h"
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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {.id=LIMINE_RSDP_REQUEST_ID};

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

    console_write("PROZEST OS " SERVICES_RELEASE "\n\n");
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
    qemu_debugcon_puts("ALLOCATOR-SAFE INVENTORY: PASS\n");
    qemu_debugcon_puts("M10.2 COMPLETE\n");
}

static struct pmm_plan allocator_plan;
static void plan_run(void)
{
    unsigned eax=0,ebx=0,ecx=0,edx=0;
    check("PMM PHYSICAL WIDTH: PASS",__get_cpuid(0x80000008,&eax,&ebx,&ecx,&edx));
    unsigned bits=eax&255;
    qemu_debugcon_puts("PMM ARCHITECTURE: RANGE-INDEXED BITMAP\n");
    check("PMM PLAN BSS PROTECTION: PASS",
          kernel_object(&allocator_plan,sizeof(allocator_plan),(uint64_t)(uintptr_t)__kernel_end));
    check("PMM METADATA SIZING: PASS",pmm_plan_build(&allocator_plan,&protected,bits));
    check("METADATA EXCLUSION VALIDATION: PASS",
          pmm_plan_validate(&allocator_plan,&protected,bits));
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3,%0":"=r"(cr3));
    struct hhdm_table_context ctx={&inventory,inventory.hhdm};
    uint64_t verified=0;
    number("MAPPING CHECK BASE: ",allocator_plan.metadata_base);
    number("MAPPING CHECK END: ",allocator_plan.metadata_end);
    int mapped=pmm_metadata_mapping_check(&allocator_plan,inventory.hhdm,cr3,bits,
                                          page_table_hhdm_resolve,&ctx,&verified);
    number("MAPPING CHECK VERIFIED: ",verified);
    check("METADATA MAPPING VALIDATION: PASS",mapped);
    number("Physical page size: ",4096);
    number("Pre-metadata eligible pages: ",allocator_plan.premetadata_pages);
    number("Bitmap capacity: ",allocator_plan.bitmap_capacity_bits);
    number("Bitmap required bytes: ",allocator_plan.bitmap_required_bytes);
    number("Bitmap storage bytes: ",allocator_plan.bitmap_storage_pages*4096);
    number("Plan BSS bytes: ",sizeof(allocator_plan));
    number("Metadata total bytes: ",allocator_plan.metadata_total_bytes);
    number("Metadata reserved pages: ",allocator_plan.bitmap_storage_pages);
    number("Unused trailing bitmap bits: ",allocator_plan.unused_trailing_bits);
    number("Proposed metadata physical base: ",allocator_plan.metadata_base);
    number("Proposed metadata physical end: ",allocator_plan.metadata_end);
    number("Metadata HHDM virtual base: ",inventory.hhdm+allocator_plan.metadata_base);
    number("Metadata mapping pages verified: ",verified);
    number("Final managed ranges: ",allocator_plan.range_count);
    number("Final managed pages: ",allocator_plan.managed_pages);
    number("Final managed bytes: ",allocator_plan.managed_bytes);
    for (uint32_t i=0;i<allocator_plan.range_count;i++) {
        const struct pmm_managed_range *r=&allocator_plan.range[i];
        qemu_debugcon_puts("MANAGED "); qemu_debugcon_hex64(r->base); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->end); qemu_debugcon_putc(' ');
        qemu_debugcon_hex64(r->first_index); qemu_debugcon_putc('\n');
        uint64_t first,last,round;
        check("MANAGED BOUNDARY ROUNDTRIP",
              pmm_page_to_index(&allocator_plan,r->base,&first) &&
              pmm_page_to_index(&allocator_plan,r->end-4096,&last) &&
              pmm_index_to_page(&allocator_plan,first,&round) && round==r->base &&
              pmm_index_to_page(&allocator_plan,last,&round) && round==r->end-4096);
        qemu_debugcon_puts("MAP_SAMPLE "); qemu_debugcon_hex64(r->base);
        qemu_debugcon_putc(' '); qemu_debugcon_hex64(first); qemu_debugcon_putc('\n');
        qemu_debugcon_puts("MAP_SAMPLE "); qemu_debugcon_hex64(r->end-4096);
        qemu_debugcon_putc(' '); qemu_debugcon_hex64(last); qemu_debugcon_putc('\n');
    }
    uint64_t rejected;
    check("PHYSICAL ADDRESS/INDEX VALIDATION: PASS",
          !pmm_page_to_index(&allocator_plan,allocator_plan.metadata_base,&rejected) &&
          !pmm_page_to_index(&allocator_plan,allocator_plan.metadata_end-4096,&rejected) &&
          !pmm_page_to_index(&allocator_plan,allocator_plan.range[0].base+1,&rejected) &&
          !pmm_index_to_page(&allocator_plan,allocator_plan.managed_pages,&rejected));
    qemu_debugcon_puts("PMM METADATA PLAN: PASS\n");
    qemu_debugcon_puts("M10.3 COMPLETE\n");
}
static void live_run(void)
{
    unsigned eax=0, ebx=0, ecx=0, edx=0;
    check("PMM LIVE PHYSICAL WIDTH: PASS", __get_cpuid(0x80000008,&eax,&ebx,&ecx,&edx));
    unsigned bits=eax&255;
    struct pmm_stats stats={1,2,3};
    uint64_t single=UINT64_C(0xfeedface), block=0, reused=0;
    check("PMM PREINIT GUARD: PASS",pmm_get_stats(&stats)==PMM_NOT_INITIALIZED &&
          stats.managed_pages==1 && stats.free_pages==2 && stats.allocated_pages==3 &&
          pmm_alloc_page(&single)==PMM_NOT_INITIALIZED && single==UINT64_C(0xfeedface) &&
          pmm_free_page(4096)==PMM_NOT_INITIALIZED &&
          pmm_alloc_contiguous(1,1,&single)==PMM_NOT_INITIALIZED && single==UINT64_C(0xfeedface) &&
          pmm_free_contiguous(4096,1)==PMM_NOT_INITIALIZED);
    check("PMM INITIALIZATION: PASS",pmm_init(&allocator_plan,&protected,&inventory,bits)==PMM_OK);
    check("PMM BITMAP READBACK: PASS",pmm_initial_state_check());
    check("PMM COUNTERS: PASS",pmm_get_stats(&stats)==PMM_OK &&
          stats.managed_pages==allocator_plan.managed_pages && stats.free_pages==stats.managed_pages &&
          !stats.allocated_pages);
    check("PMM METADATA EXCLUDED: PASS",!pmm_is_managed_address(0) &&
          !pmm_is_managed_address(allocator_plan.metadata_base) &&
          !pmm_is_managed_address(allocator_plan.metadata_end-4096));
    number("PMM BITMAP BASE: ",allocator_plan.metadata_base);
    number("PMM BITMAP BYTES: ",allocator_plan.bitmap_storage_pages*4096);
    number("PMM MANAGED PAGES: ",stats.managed_pages);
    number("PMM FREE PAGES: ",stats.free_pages);
    number("PMM ALLOCATED PAGES: ",stats.allocated_pages);
    number("PMM UNAVAILABLE BITS: ",allocator_plan.bitmap_capacity_bits-stats.managed_pages);
    qemu_debugcon_puts("PHYSICAL MEMORY ALLOCATOR: INITIALIZED\n");
    /* Bounded boot smoke test. No page payload access or deliberate faults. */
    check("RC2 SINGLE ALLOCATION: PASS",pmm_alloc_page(&single)==PMM_OK &&
          single==allocator_plan.range[0].base && pmm_is_managed_address(single));
    number("RC2 SINGLE: ",single);
    check("RC2 CONTIGUOUS ALLOCATION: PASS",pmm_alloc_contiguous(8,8,&block)==PMM_OK &&
          block%32768==0 && block<=UINT64_MAX-32768 &&
          (single<block || single>=block+32768));
    number("RC2 BLOCK: ",block);
    check("RC2 LIVE COUNTERS: PASS",pmm_get_stats(&stats)==PMM_OK &&
          stats.allocated_pages==9 && stats.free_pages==stats.managed_pages-9 && pmm_live_state_check());
    check("RC2 REINIT GUARD: PASS",pmm_init(NULL,NULL,NULL,0)==PMM_ALREADY_INITIALIZED &&
          pmm_get_stats(&stats)==PMM_OK && stats.allocated_pages==9 && pmm_live_state_check());
    check("RC2 CONTIGUOUS RELEASE: PASS",pmm_free_contiguous(block,8)==PMM_OK &&
          pmm_free_contiguous(block,8)==PMM_DOUBLE_FREE);
    check("RC2 SINGLE RELEASE: PASS",pmm_free_page(single)==PMM_OK &&
          pmm_free_page(single)==PMM_DOUBLE_FREE);
    check("RC2 SINGLE REUSE: PASS",pmm_alloc_page(&reused)==PMM_OK && reused==single &&
          pmm_free_page(reused)==PMM_OK);
    number("RC2 REUSED: ",reused);
    check("RC2 FINAL PMM STATE: PASS",pmm_get_stats(&stats)==PMM_OK &&
          stats.managed_pages==allocator_plan.managed_pages && !stats.allocated_pages &&
          stats.free_pages==stats.managed_pages && pmm_live_state_check());
    number("RC2 FINAL FREE: ",stats.free_pages);
    number("RC2 FINAL ALLOCATED: ",stats.allocated_pages);
    log_write(LOG_OK,"Physical memory manager initialized");
    log_write(LOG_OK,"Page allocation and release verified");
    console_write("Managed memory (MiB): ");
    char digits[21]; unsigned pos=20; digits[pos]=0;
    uint64_t mib=stats.managed_pages/256;
    do { digits[--pos]=(char)('0'+mib%10); mib/=10; } while (mib);
    console_write(digits+pos); console_write("\n");
    console_write("All managed pages free\n");
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
    qemu_debugcon_puts("MEMORY INVENTORY: PASS\n");
}

__attribute__((section(".text.kernel_entry"), noreturn))
#include "vmm.h"
#include "vmm_boot.inc"
#include "irq_services.inc"
#include "heap_boot.inc"
#if PROZEST_STAGE >= 14
#include "device_boot.inc"
#endif
#if PROZEST_STAGE >= 15
#include "scheduler_boot.inc"
#endif

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
    inventory_run();
    reservation_run();
    plan_run();
    live_run();
    vmm_run();
    irq_run();
    qemu_debugcon_puts("M12 COMPLETE\n");
    heap_run();
#if PROZEST_STAGE >= 14
    device_run();
#endif
#if PROZEST_STAGE >= 15
    scheduler_run();
#endif
    console_newline();
    console_write("KERNEL READY - " SERVICES_MILESTONE "\n");
    qemu_debugcon_puts("KERNEL READY - " SERVICES_MILESTONE "\n");
    qemu_debugcon_puts("SERVICES COMPLETE\n");
    transition_checkpoint_halt();
}
