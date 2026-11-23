#ifndef PROZEST_CPU_TABLES_PREFLIGHT_H
#define PROZEST_CPU_TABLES_PREFLIGHT_H

#include "bootstrap_tables.h"
#include "bootstrap_idt.h"

/*
 * Validate prepared CPU descriptor structures.
 *
 * Memory-only checks.
 *
 * Does NOT execute LGDT, LIDT, LTR or STI.
 *
 * Returns:
 *   1 = structural checks passed
 *   0 = invalid or missing configuration
 *
 * Passing does not prove installation safety.
 */
int cpu_tables_preflight_check(
    const struct bootstrap_cpu_tables *cpu,
    const struct bootstrap_idt_state *irq
);

#endif
