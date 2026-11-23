#ifndef PROZEST_BOOTSTRAP_IDT_H
#define PROZEST_BOOTSTRAP_IDT_H

#include "idt.h"

/*
 * Permanent IDT and its IDTR operand.
 * Bootstrap processor only.
 */
struct bootstrap_idt_state {
    struct idt_table table;
    struct idt_pointer idtr;
};

/*
 * Prepare the IDT in permanent memory.
 * Does not execute LIDT or STI.
 */
int bootstrap_idt_prepare(void);

/*
 * Returns NULL until preparation succeeds.
 */
const struct bootstrap_idt_state *
bootstrap_idt_get(void);

#endif
