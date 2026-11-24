/* BSP-only, preparation-only Limine entry point. */
#include <limine.h>
#include <stdint.h>

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

enum state_field {
    GDTR_BASE, GDTR_LIMIT, IDTR_BASE, IDTR_LIMIT, CS, SS, DS, ES, FS, GS, TR,
    FS_BASE, GS_BASE, KERNEL_GS_BASE, CR3, CR4, INTERRUPT_FLAG, FIELD_COUNT
};

static const char *const field_name[] = {
    "GDTR_BASE", "GDTR_LIMIT", "IDTR_BASE", "IDTR_LIMIT", "CS", "SS", "DS",
    "ES", "FS", "GS", "TR", "FS_BASE", "GS_BASE", "KERNEL_GS_BASE", "CR3",
    "CR4", "IF"
};
_Static_assert(sizeof(field_name) / sizeof(field_name[0]) == FIELD_COUNT,
               "Every captured field needs a debugcon label");

struct handoff_state {
    uint64_t value[FIELD_COUNT];
};

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

static int supported_handoff(const struct handoff_state *state)
{
    const uint64_t *v = state->value;
    return v[GDTR_BASE] != 0 && v[GDTR_LIMIT] == 0x47 &&
           v[IDTR_BASE] == 0 && v[IDTR_LIMIT] == 0 &&
           v[CS] == TRANSITION_INHERITED_CS && v[SS] == TRANSITION_INHERITED_DATA &&
           v[DS] == v[SS] && v[ES] == v[SS] && v[FS] == v[SS] && v[GS] == v[SS] &&
           v[TR] == 0x38 && v[FS_BASE] == 0 && v[GS_BASE] == 0 &&
           v[INTERRUPT_FLAG] == 0 && !(v[CR4] & (1u << 12)) &&
           rights_match(v[CS], 0x60fe00, 0x209a00) &&
           rights_match(v[DS], 0xfe00, 0x9200);
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
    qemu_debugcon_puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed)
        transition_checkpoint_halt();
}

__attribute__((section(".text.kernel_entry"), noreturn))
void kernel_main(void)
{
    struct handoff_state before, after;
    debugcon_probe();
    capture(&before);
    print_state("BEFORE", &before);
    check("HANDOFF", LIMINE_BASE_REVISION_SUPPORTED(revision) &&
                     supported_handoff(&before));

    check("EARLY IDT", descriptor_transition_prepare_early_idt() != NULL);

    /* A second entry must never reinitialize potentially live storage. */
    int fresh = !bootstrap_cpu_tables_get() && !bootstrap_idt_get();
    int ready = fresh && bootstrap_cpu_tables_prepare() && bootstrap_idt_prepare();
    const struct bootstrap_cpu_tables *cpu = bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *irq = bootstrap_idt_get();
    check("FINAL TABLES", ready && cpu_tables_preflight_check(cpu, irq) &&
                          exception_gate_identity_check(&irq->table));
    check("TRANSITION TABLES", descriptor_transition_prepare() &&
          descriptor_transition_validate(descriptor_transition_get(), cpu, irq));

    capture(&after);
    print_state("AFTER", &after);
    int unchanged = 1;
    for (unsigned int i = 0; i < FIELD_COUNT; ++i)
        unchanged &= before.value[i] == after.value[i];
    check("CPU STATE UNCHANGED", unchanged);
    qemu_debugcon_puts("TRANSITION PREPARATION: PASS\n");
    transition_checkpoint_halt();
}
