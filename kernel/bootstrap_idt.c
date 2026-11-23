#include <stddef.h>
#include <stdint.h>

#include "bootstrap_idt.h"
#include "idt_exceptions.h"

/* Permanent kernel memory. */
static struct bootstrap_idt_state bootstrap_idt_storage
    __attribute__((aligned(16)));

static int bootstrap_idt_ready = 0;

int bootstrap_idt_prepare(void)
{
    bootstrap_idt_ready = 0;

    /* Build all 32 CPU exception gates. */
    if (!idt_exceptions_prepare(
            &bootstrap_idt_storage.table
        )) {
        return 0;
    }

    /* IDTR limit = table size - 1. */
    bootstrap_idt_storage.idtr.limit =
        (uint16_t)(
            sizeof(bootstrap_idt_storage.table) - 1u
        );

    /* IDTR base = persistent IDT address. */
    bootstrap_idt_storage.idtr.base =
        (uint64_t)(uintptr_t)&bootstrap_idt_storage.table;

    bootstrap_idt_ready = 1;
    return 1;
}

const struct bootstrap_idt_state *
bootstrap_idt_get(void)
{
    if (!bootstrap_idt_ready) {
        return NULL;
    }

    return &bootstrap_idt_storage;
}
