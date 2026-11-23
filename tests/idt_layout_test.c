#include <stdio.h>
#include <stddef.h>

#include "idt.h"

static int failures = 0;

static void check(
    const char *name,
    size_t actual,
    size_t expected
)
{
    if (actual == expected) {
        printf("[PASS] %s: %zu\n", name, actual);
    } else {
        printf(
            "[FAIL] %s: expected %zu, got %zu\n",
            name, expected, actual
        );
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - IDT Layout Tests\n");
    printf("============================\n");

    check(
        "IDT entry size",
        sizeof(struct idt_entry),
        16
    );

    check(
        "IDTR pointer size",
        sizeof(struct idt_pointer),
        10
    );

    check(
        "IDT maximum entries",
        IDT_MAX_ENTRIES,
        256
    );

    check(
        "Selector offset",
        offsetof(struct idt_entry, selector),
        2
    );

    check(
        "IST offset",
        offsetof(struct idt_entry, ist),
        4
    );

    check(
        "Attributes offset",
        offsetof(struct idt_entry, type_attributes),
        5
    );

    check(
        "High address offset",
        offsetof(struct idt_entry, offset_high),
        8
    );

    check(
        "IDTR base offset",
        offsetof(struct idt_pointer, base),
        2
    );

    printf("----------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
