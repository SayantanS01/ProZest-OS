#ifndef PROZEST_IDT_EXCEPTIONS_H
#define PROZEST_IDT_EXCEPTIONS_H

#include "idt.h"

/*
 * Construct the 32 CPU exception gates.
 *
 * The remaining 224 entries are not present.
 *
 * Returns 1 on success and 0 for NULL.
 *
 * Does not execute LIDT.
 */
int idt_exceptions_prepare(struct idt_table *table);

#endif
