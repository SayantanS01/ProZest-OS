#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "idt.h"

static int failures = 0;

static void check(const char *name, int passed)
{
    printf("[%s] %s\n",
           passed ? "PASS" : "FAIL",
           name);

    if (!passed) {
        failures++;
    }
}

static uint64_t reconstruct_address(
    const struct idt_entry *entry
)
{
    return
        (uint64_t)entry->offset_low |
        ((uint64_t)entry->offset_middle << 16) |
        ((uint64_t)entry->offset_high << 32);
}

int main(void)
{
    printf("ProZest OS - IDT Table Tests\n");
    printf("===========================\n");

    struct idt_table table;

    /* Fill with nonzero bytes before initialization. */
    memset(&table, 0xFF, sizeof(table));

    idt_table_init(&table);

    int all_zero = 1;

    const unsigned char *bytes =
        (const unsigned char *)&table;

    for (size_t i = 0; i < sizeof(table); i++) {
        if (bytes[i] != 0) {
            all_zero = 0;
            break;
        }
    }

    check("Full table cleared", all_zero);

    check(
        "Table occupies 4096 bytes",
        sizeof(table) == 4096
    );

    int result = idt_table_set_gate(
        &table,
        0,
        0xFFFFFFFF80001000ULL,
        0x0008u,
        0,
        IDT_INTERRUPT_GATE
    );

    check("Install vector 0", result == 1);

    check(
        "Vector 0 address",
        reconstruct_address(&table.entries[0]) ==
            0xFFFFFFFF80001000ULL
    );

    check(
        "Vector 0 attributes",
        table.entries[0].type_attributes ==
            IDT_INTERRUPT_GATE
    );

    result = idt_table_set_gate(
        &table,
        255,
        0xFFFFFFFF80002000ULL,
        0x0008u,
        0,
        IDT_INTERRUPT_GATE
    );

    check("Install vector 255", result == 1);

    check(
        "Vector 255 address",
        reconstruct_address(&table.entries[255]) ==
            0xFFFFFFFF80002000ULL
    );

    /* Ensure an unused entry remains empty. */
    check(
        "Unused vector remains not present",
        table.entries[14].type_attributes == 0
    );

    /* Attempt to write beyond the last legal vector. */
    struct idt_table before = table;

    result = idt_table_set_gate(
        &table,
        256,
        0x1234u,
        0x0008u,
        0,
        IDT_INTERRUPT_GATE
    );

    check("Reject vector 256", result == 0);

    check(
        "Invalid vector does not modify table",
        memcmp(&before, &table, sizeof(table)) == 0
    );

    /*
     * NULL initialization must return without crashing.
     * Verify that an existing table remains unchanged.
     */
    struct idt_table snapshot = table;

    idt_table_init(NULL);

    check(
        "NULL initialization returns safely",
        memcmp(&snapshot, &table, sizeof(table)) == 0
    );

    check(
        "Reject NULL table installation",
        idt_table_set_gate(
            NULL, 0, 0, 0, 0, 0
        ) == 0
    );

    printf("---------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
