#include <stdio.h>
#include <stdint.h>

#include "idt.h"

static int failures = 0;

static void check(
    const char *name,
    uint64_t actual,
    uint64_t expected
)
{
    if (actual == expected) {
        printf("[PASS] %s\n", name);
    } else {
        printf(
            "[FAIL] %s: expected 0x%llx, got 0x%llx\n",
            name,
            (unsigned long long)expected,
            (unsigned long long)actual
        );
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - IDT Gate Tests\n");
    printf("==========================\n");

    struct idt_entry entry = {0};

    idt_encode_gate(
        &entry,
        0x123456789ABCDEF0ULL,
        0x0008u,
        3,
        IDT_INTERRUPT_GATE
    );

    check("Low address", entry.offset_low, 0xDEF0u);
    check("Middle address", entry.offset_middle, 0x9ABCu);
    check("High address", entry.offset_high, 0x12345678u);
    check("Code selector", entry.selector, 0x0008u);
    check("IST index", entry.ist, 3);
    check("Gate attributes", entry.type_attributes, 0x8Eu);
    check("Reserved field", entry.reserved, 0);

    uint64_t reconstructed =
        (uint64_t)entry.offset_low |
        ((uint64_t)entry.offset_middle << 16) |
        ((uint64_t)entry.offset_high << 32);

    check(
        "Full address reconstruction",
        reconstructed,
        0x123456789ABCDEF0ULL
    );

    /*
     * The IST field has only three usable bits.
     * 0xFF must be masked to 0x07.
     */
    idt_encode_gate(
        &entry,
        0,
        0x0008u,
        0xFFu,
        IDT_INTERRUPT_GATE
    );

    check("IST masking", entry.ist, 7);

    /*
     * NULL is safely ignored.
     */
    idt_encode_gate(
        NULL,
        0,
        0,
        0,
        0
    );

    printf("--------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
