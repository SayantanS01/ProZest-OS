#include <stddef.h>
#include <stdint.h>

#include "bootstrap_tables.h"

/*
 * Static storage remains allocated for
 * the lifetime of the running kernel.
 *
 * This is not a function-local stack object.
 */
static struct bootstrap_cpu_tables bootstrap_storage
    __attribute__((aligned(16)));

static int bootstrap_ready = 0;

/*
 * Prepare the bootstrap CPU's GDT and TSS.
 *
 * No privileged register modifications.
 */
int bootstrap_cpu_tables_prepare(void)
{
    bootstrap_ready = 0;

    /* Configure three emergency IST stacks. */
    tss_prepare_bootstrap(&bootstrap_storage.tss);

    /* Encode the kernel GDT and TSS descriptor. */
    if (!gdt_table_init(
            &bootstrap_storage.gdt,
            &bootstrap_storage.tss
        )) {
        return 0;
    }

    /* Encode the GDTR address and limit. */
    if (!gdt_pointer_init(
            &bootstrap_storage.gdtr,
            &bootstrap_storage.gdt
        )) {
        return 0;
    }

    bootstrap_ready = 1;

    return 1;
}

/*
 * Access the prepared tables without
 * modifying processor state.
 */
const struct bootstrap_cpu_tables *
bootstrap_cpu_tables_get(void)
{
    if (!bootstrap_ready) {
        return NULL;
    }

    return &bootstrap_storage;
}
