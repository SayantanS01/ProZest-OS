#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "descriptor_transition.h"

static unsigned checks;
static unsigned failures;

static void check(const char *label, int result)
{
    ++checks;
    if (!result) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", label);
    }
}

/* The host executable links final and temporary entry symbols for address
 * comparison. All code paths exercised here operate on ordinary memory. */
_Noreturn void exception_dispatch(uint64_t vector, uint64_t error)
{
    fprintf(stderr, "Unexpected dispatch %llu/%llu\n",
            (unsigned long long)vector, (unsigned long long)error);
    abort();
}

#define VECTORS(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) \
    X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)
#define DECLARE(n) extern void transition_fatal_##n(void);
VECTORS(DECLARE)
#define ADDRESS(n) transition_fatal_##n,
static void (*const expected_gate[32])(void) = { VECTORS(ADDRESS) };
#undef DECLARE
#undef ADDRESS
#undef VECTORS

static uint64_t offset(const struct idt_entry *gate)
{
    return (uint64_t)gate->offset_low | ((uint64_t)gate->offset_middle << 16) |
           ((uint64_t)gate->offset_high << 32);
}

static struct descriptor_transition *tables;
static struct bootstrap_cpu_tables *cpu;
static struct bootstrap_idt_state *irq;

static int valid(void)
{
    return descriptor_transition_validate(tables, cpu, irq);
}

/* All mutated objects are writable static host fixtures. */
#define REJECT_FIELD(field, bad) do { \
    __auto_type saved = (field); \
    (field) = (bad); \
    check("reject " #field, !valid()); \
    (field) = saved; \
} while (0)

static void check_idt(struct bootstrap_idt_state *image, uint16_t selector)
{
    check("IDTR references its own complete table",
          image->idtr.base == (uintptr_t)&image->table && image->idtr.limit == 4095);
    REJECT_FIELD(image->idtr.base, irq->idtr.base);
    REJECT_FIELD(image->idtr.limit, 4096);

    for (unsigned v = 0; v < 32; ++v) {
        struct idt_entry *gate = &image->table.entries[v];
        char name[96];
        snprintf(name, sizeof(name), "gate %u with CS %#x resolves to its fatal stub", v, selector);
        check(name, offset(gate) == (uintptr_t)expected_gate[v] && gate->selector == selector &&
                    gate->type_attributes == 0x8e && gate->ist == 0 && gate->reserved == 0);
        /* A real, executable handler of the wrong vector still must be rejected. */
        REJECT_FIELD(*gate, image->table.entries[(v + 1) % 32]);
    }
    struct idt_entry *nmi = &image->table.entries[2];
    REJECT_FIELD(nmi->ist, 2);
    REJECT_FIELD(nmi->selector, selector ^ 0x20u);
    REJECT_FIELD(nmi->type_attributes, 0xee);
    REJECT_FIELD(nmi->reserved, 1);
    REJECT_FIELD(nmi->ist, 0x80);
    REJECT_FIELD(nmi->offset_high, 0x00010000u);

    struct idt_entry zero = {0};
    for (unsigned v = 32; v < IDT_MAX_ENTRIES; ++v) {
        check("unused gate zeroed", memcmp(&image->table.entries[v], &zero, sizeof(zero)) == 0);
    }
    REJECT_FIELD(image->table.entries[32].offset_middle, 1);
    REJECT_FIELD(image->table.entries[255].type_attributes, 0x8e);
}

int main(void)
{
    check("unpublished before preparation", descriptor_transition_get() == NULL);
    struct bootstrap_idt_state *early =
        (struct bootstrap_idt_state *)descriptor_transition_prepare_early_idt();
    check("early IDT prepared before final tables", early != NULL);
    if (!early)
        return EXIT_FAILURE;
    check("early stage prepares only its IDT", !descriptor_transition_get() &&
          !bootstrap_cpu_tables_get() && !bootstrap_idt_get());
    check("early operand references persistent IDT",
          early->idtr.base == (uintptr_t)&early->table && early->idtr.limit == 4095);
    for (unsigned v = 0; v < 32; ++v) {
        const struct idt_entry *gate = &early->table.entries[v];
        check("early gate usable before full preparation", gate->selector == 0x28 &&
              gate->ist == 0 && gate->type_attributes == 0x8e && gate->reserved == 0 &&
              offset(gate) == (uintptr_t)expected_gate[v]);
    }
    check("repeated early prepare retains its address", descriptor_transition_prepare_early_idt() == early);
    early->table.entries[8].ist = 1;
    check("re-entry rejects a damaged early gate", !descriptor_transition_prepare_early_idt());
    check("early prepare preserves the damaged gate", early->table.entries[8].ist == 1);
    check("missing both prerequisite tables", !descriptor_transition_prepare());
    check("final CPU tables prepared", bootstrap_cpu_tables_prepare());
    check("missing final IDT", !descriptor_transition_prepare());
    check("failed prepare left transition unpublished", descriptor_transition_get() == NULL);
    check("final IDT prepared", bootstrap_idt_prepare());
    check("full preparation rejects damaged published early IDT", !descriptor_transition_prepare());
    check("full preparation leaves early gate untouched", early->table.entries[8].ist == 1);
    check("full preparation failed before publication", !descriptor_transition_get());
    early->table.entries[8].ist = 0;
    check("early IDT usable again after host fixture restoration", descriptor_transition_prepare_early_idt() == early);
    cpu = (struct bootstrap_cpu_tables *)bootstrap_cpu_tables_get();
    if (!cpu)
        return EXIT_FAILURE;
    cpu->tss.reserved1 = 1;
    check("reject invalid final TSS before publication", !descriptor_transition_prepare());
    check("failed validation leaves transition unpublished", descriptor_transition_get() == NULL);
    cpu->tss.reserved1 = 0;
    check("transition prepared", descriptor_transition_prepare());

    tables = (struct descriptor_transition *)descriptor_transition_get();
    irq = (struct bootstrap_idt_state *)bootstrap_idt_get();
    if (!tables || !irq)
        return EXIT_FAILURE;

    check("full transition uses the original published early IDT", &tables->inherited_idt == early);
    check("valid transition", valid());
    check("missing transition rejected", !descriptor_transition_validate(NULL, cpu, irq));
    check("missing final GDT rejected", !descriptor_transition_validate(tables, NULL, irq));
    check("missing final IDT rejected", !descriptor_transition_validate(tables, cpu, NULL));

    check("GDTR references full bridge", tables->gdtr.base == (uintptr_t)&tables->gdt &&
          tables->gdtr.limit == 0x37);
    check("bridge old code selector remains usable", tables->gdt[0x28 / 8] == GDT_KERNEL_CODE_DESCRIPTOR);
    check("bridge old data selector remains usable", tables->gdt[0x30 / 8] == GDT_KERNEL_DATA_DESCRIPTOR);
    check("bridge new code selector matches final GDT",
          tables->gdt[0x08 / 8] == cpu->gdt.kernel_code_descriptor);
    check("bridge new data selector matches final GDT",
          tables->gdt[0x10 / 8] == cpu->gdt.kernel_data_descriptor);
    REJECT_FIELD(tables->gdtr.base, cpu->gdtr.base);
    REJECT_FIELD(tables->gdtr.limit, 0x27);
    REJECT_FIELD(tables->gdt[0], 1u);
    REJECT_FIELD(tables->gdt[1], 0u);
    REJECT_FIELD(tables->gdt[2], 0u);
    REJECT_FIELD(tables->gdt[3], 1u);
    REJECT_FIELD(tables->gdt[4], 1u);
    REJECT_FIELD(tables->gdt[5], 0u);
    REJECT_FIELD(tables->gdt[6], 0u);

    check_idt(&tables->inherited_idt, 0x28);
    check_idt(&tables->target_idt, 0x08);
    REJECT_FIELD(cpu->gdt.tss_descriptor.access, 0x8b);
    REJECT_FIELD(cpu->tss.reserved0, 1);
    REJECT_FIELD(cpu->tss.reserved1, 1);
    REJECT_FIELD(cpu->tss.reserved2, 1);
    REJECT_FIELD(cpu->tss.reserved3, 1);
    for (unsigned i = 0; i < 3; ++i) {
        REJECT_FIELD(cpu->tss.rsp[i], cpu->tss.ist[i]);
    }
    /* Swap valid, aligned dedicated stack tops without creating overlap. */
    uint64_t df = cpu->tss.ist[0];
    cpu->tss.ist[0] = cpu->tss.ist[1];
    cpu->tss.ist[1] = df;
    check("DF and NMI stacks have exact identities", !valid());
    cpu->tss.ist[1] = cpu->tss.ist[0];
    cpu->tss.ist[0] = df;

    REJECT_FIELD(irq->table.entries[0], irq->table.entries[1]);
    tables->gdt[3] = 1;
    check("second prepare rejects damaged bridge", !descriptor_transition_prepare());
    check("second prepare leaves damage observable", tables->gdt[3] == 1);
    tables->gdt[3] = 0;
    check("restored state accepted", valid() && descriptor_transition_prepare());
    check("stable published address", descriptor_transition_get() == tables);

    printf("Checks: %u\nFailures: %u\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
