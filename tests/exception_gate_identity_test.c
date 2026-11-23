#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "idt.h"
#include "idt_exceptions.h"
#include "exception_gate_identity.h"

extern void exception_stub_0(void);
extern void exception_stub_1(void);

/*
 * Link-only dispatcher. Never run the stubs
 * in this Ubuntu host-side test.
 */
__attribute__((noreturn))
void exception_dispatch(uint64_t vector, uint64_t error)
{
    fprintf(
        stderr,
        "UNEXPECTED STUB: %llu / %llu\n",
        (unsigned long long)vector,
        (unsigned long long)error
    );

    abort();
}

static int checks = 0;
static int failures = 0;

static void check(const char *name, int passed)
{
    checks++;

    printf("[%s] %s\n",
           passed ? "PASS" : "FAIL",
           name);

    if (!passed) {
        failures++;
    }
}

static void replace_handler(
    struct idt_entry *entry,
    uint64_t address
)
{
    entry->offset_low =
        (uint16_t)(address & 0xFFFFu);

    entry->offset_middle =
        (uint16_t)((address >> 16) & 0xFFFFu);

    entry->offset_high =
        (uint32_t)(address >> 32);
}

int main(void)
{
    puts("ProZest OS - Exception Handler Identity");
    puts("======================================");

    static struct idt_table table;

    check(
        "NULL table rejected",
        exception_gate_identity_check(NULL) == 0
    );

    check(
        "Exception table preparation",
        idt_exceptions_prepare(&table) == 1
    );

    check(
        "All 32 correct handlers accepted",
        exception_gate_identity_check(&table) == 1
    );

    /*
     * Deliberately redirect one vector
     * at a time to a different real stub.
     *
     * The substituted address is valid,
     * but incorrect for that vector.
     */
    for (unsigned int vector = 0; vector < 32; vector++) {
        struct idt_entry original =
            table.entries[vector];

        uint64_t wrong_handler =
            (uint64_t)(uintptr_t)(
                vector == 0
                ? exception_stub_1
                : exception_stub_0
            );

        replace_handler(
            &table.entries[vector],
            wrong_handler
        );

        char label[80];

        snprintf(
            label,
            sizeof(label),
            "Wrong handler rejected for vector %02u",
            vector
        );

        check(
            label,
            exception_gate_identity_check(&table) == 0
        );

        table.entries[vector] = original;
    }

    check(
        "All 32 handlers valid after restoration",
        exception_gate_identity_check(&table) == 1
    );

    puts("--------------------------------------");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
