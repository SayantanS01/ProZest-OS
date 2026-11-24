#ifndef PROZEST_DESCRIPTOR_TRANSITION_H
#define PROZEST_DESCRIPTOR_TRANSITION_H

#include "bootstrap_idt.h"
#include "bootstrap_tables.h"

#define TRANSITION_INHERITED_CS 0x28u
#define TRANSITION_INHERITED_DATA 0x30u
#define TRANSITION_EXCEPTION_COUNT 32u

/* All gates use IST0. Only CPL0 same-privilege delivery is supported. */
struct descriptor_transition {
    uint64_t gdt[7];
    struct gdt_pointer gdtr;
    _Alignas(16) struct bootstrap_idt_state inherited_idt;
    _Alignas(16) struct bootstrap_idt_state target_idt;
};

_Static_assert(sizeof(((struct descriptor_transition *)0)->gdt) == 0x38,
               "Bridge GDT must include both selector pairs");
_Static_assert(offsetof(struct idt_entry, selector) == 2 &&
               offsetof(struct idt_entry, ist) == 4 &&
               offsetof(struct idt_entry, type_attributes) == 5 &&
               offsetof(struct idt_entry, offset_middle) == 6 &&
               offsetof(struct idt_entry, offset_high) == 8 &&
               offsetof(struct idt_entry, reserved) == 12,
               "Long-mode interrupt gate layout");

/* Prepare the inherited-selector IST0 IDT independently of all other tables.
 * Once published, every call revalidates this table without rewriting it.
 * Before loading it, the caller must verify the active GDT and current stack. */
const struct bootstrap_idt_state *descriptor_transition_prepare_early_idt(void);

/* Pre-installation checks. Each final table must already have been prepared.
 * Repeated successful preparation only validates the published storage.
 * Single BSP only. Never call the bootstrap initializers on live tables. */
int descriptor_transition_prepare(void);
const struct descriptor_transition *descriptor_transition_get(void);
int descriptor_transition_validate(
    const struct descriptor_transition *tables,
    const struct bootstrap_cpu_tables *cpu,
    const struct bootstrap_idt_state *irq
);

extern void (*const transition_fatal_handlers[TRANSITION_EXCEPTION_COUNT])(void);

#endif
