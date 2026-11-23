#include <stddef.h>
#include <stdint.h>

#include "cpu_tables_preflight.h"
#include "gdt.h"
#include "idt.h"
#include "tss.h"

/*
 * Validate canonical 48-bit virtual addresses.
 *
 * This matches our current higher-half,
 * four-level x86-64 addressing design.
 */
static int canonical48(uint64_t address)
{
    uint64_t upper = address >> 48;

    if ((address & (1ULL << 47)) != 0) {
        return upper == 0xFFFFu;
    }

    return upper == 0;
}

static uint64_t tss_descriptor_base(
    const struct gdt_tss_descriptor *d
)
{
    return
        (uint64_t)d->base_low |
        ((uint64_t)d->base_middle << 16) |
        ((uint64_t)d->base_high << 24) |
        ((uint64_t)d->base_upper << 32);
}

static uint64_t idt_handler_address(
    const struct idt_entry *entry
)
{
    return
        (uint64_t)entry->offset_low |
        ((uint64_t)entry->offset_middle << 16) |
        ((uint64_t)entry->offset_high << 32);
}

static uint8_t expected_ist(unsigned int vector)
{
    if (vector == 2u) {
        return TSS_IST_NMI;
    }

    if (vector == 8u) {
        return TSS_IST_DOUBLE_FAULT;
    }

    if (vector == 18u) {
        return TSS_IST_MACHINE_CHECK;
    }

    return 0;
}

static int valid_stack_top(uint64_t top)
{
    return
        top >= TSS_IST_STACK_SIZE &&
        (top & 15u) == 0 &&
        canonical48(top);
}

static int separate_stacks(uint64_t a, uint64_t b)
{
    uint64_t distance = a > b ? a - b : b - a;

    return distance >= TSS_IST_STACK_SIZE;
}

/*
 * Check memory structures before attempting
 * any privileged descriptor installation.
 */
int cpu_tables_preflight_check(
    const struct bootstrap_cpu_tables *cpu,
    const struct bootstrap_idt_state *irq
)
{
    if (cpu == NULL || irq == NULL) {
        return 0;
    }

    /* Verify the GDT location and size. */
    if (cpu->gdtr.limit != sizeof(cpu->gdt) - 1u ||
        cpu->gdtr.base != (uint64_t)(uintptr_t)&cpu->gdt ||
        !canonical48(cpu->gdtr.base)) {
        return 0;
    }

    /* Verify the IDT location and size. */
    if (irq->idtr.limit != sizeof(irq->table) - 1u ||
        irq->idtr.base !=
            (uint64_t)(uintptr_t)&irq->table ||
        !canonical48(irq->idtr.base)) {
        return 0;
    }

    /* Verify the ring-0 code and data descriptors. */
    if (cpu->gdt.null_descriptor != 0 ||
        cpu->gdt.kernel_code_descriptor !=
            GDT_KERNEL_CODE_DESCRIPTOR ||
        cpu->gdt.kernel_data_descriptor !=
            GDT_KERNEL_DATA_DESCRIPTOR) {
        return 0;
    }

    /* Verify the available 64-bit TSS descriptor. */
    const struct gdt_tss_descriptor *desc =
        &cpu->gdt.tss_descriptor;

    if (desc->access != GDT_TSS_ACCESS ||
        desc->limit_low != sizeof(cpu->tss) - 1u ||
        desc->limit_high_flags != 0 ||
        desc->reserved != 0 ||
        tss_descriptor_base(desc) !=
            (uint64_t)(uintptr_t)&cpu->tss) {
        return 0;
    }

    if (!canonical48(tss_descriptor_base(desc))) {
        return 0;
    }

    if (cpu->tss.iomap_base != sizeof(cpu->tss)) {
        return 0;
    }

    /* Verify the three dedicated IST stack tops. */
    uint64_t df = cpu->tss.ist[0];
    uint64_t nmi = cpu->tss.ist[1];
    uint64_t mc = cpu->tss.ist[2];

    if (!valid_stack_top(df) ||
        !valid_stack_top(nmi) ||
        !valid_stack_top(mc) ||
        !separate_stacks(df, nmi) ||
        !separate_stacks(df, mc) ||
        !separate_stacks(nmi, mc)) {
        return 0;
    }

    /* Remaining IST slots must be unused. */
    for (unsigned int i = 3; i < TSS_IST_COUNT; i++) {
        if (cpu->tss.ist[i] != 0) {
            return 0;
        }
    }

    /* Verify all 32 installed exception gates. */
    for (unsigned int vector = 0; vector < 32; vector++) {
        const struct idt_entry *gate =
            &irq->table.entries[vector];

        uint64_t handler = idt_handler_address(gate);

        if (gate->selector != GDT_KERNEL_CODE_SELECTOR ||
            gate->type_attributes != IDT_INTERRUPT_GATE ||
            gate->ist != expected_ist(vector) ||
            gate->reserved != 0 ||
            handler == 0 ||
            !canonical48(handler)) {
            return 0;
        }
    }

    /* Ensure vectors 32-255 contain no descriptors. */
    for (unsigned int vector = 32;
         vector < IDT_MAX_ENTRIES;
         vector++) {

        const struct idt_entry *gate =
            &irq->table.entries[vector];

        if (gate->offset_low != 0 ||
            gate->offset_middle != 0 ||
            gate->offset_high != 0 ||
            gate->selector != 0 ||
            gate->ist != 0 ||
            gate->type_attributes != 0 ||
            gate->reserved != 0) {
            return 0;
        }
    }

    return 1;
}
