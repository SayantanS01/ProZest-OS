#ifndef PROZEST_BOOTSTRAP_TABLES_H
#define PROZEST_BOOTSTRAP_TABLES_H

#include "gdt.h"
#include "tss.h"

/*
 * Permanent bootstrap processor tables.
 *
 * This initial implementation supports
 * one CPU. Multiprocessor support will
 * require per-CPU TSS structures.
 */
struct bootstrap_cpu_tables {
    struct tss64 tss;
    struct gdt_table gdt;
    struct gdt_pointer gdtr;
};

/*
 * Prepare all structures in memory.
 *
 * Must only be called before activating
 * these tables on the processor.
 *
 * Does not execute LGDT, LTR or LIDT.
 */
int bootstrap_cpu_tables_prepare(void);

/*
 * Return the prepared structures.
 *
 * Returns NULL until preparation succeeds.
 */
const struct bootstrap_cpu_tables *
bootstrap_cpu_tables_get(void);

#endif
