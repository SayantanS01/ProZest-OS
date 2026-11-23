#include <stddef.h>
#include <stdint.h>

#include "exception_gate_identity.h"

/*
 * Symbols provided by exception_stubs.S.
 */
#define DECLARE(n) extern void exception_stub_##n(void);

DECLARE(0)  DECLARE(1)  DECLARE(2)  DECLARE(3)
DECLARE(4)  DECLARE(5)  DECLARE(6)  DECLARE(7)
DECLARE(8)  DECLARE(9)  DECLARE(10) DECLARE(11)
DECLARE(12) DECLARE(13) DECLARE(14) DECLARE(15)
DECLARE(16) DECLARE(17) DECLARE(18) DECLARE(19)
DECLARE(20) DECLARE(21) DECLARE(22) DECLARE(23)
DECLARE(24) DECLARE(25) DECLARE(26) DECLARE(27)
DECLARE(28) DECLARE(29) DECLARE(30) DECLARE(31)

#undef DECLARE

/*
 * Expected handler for every exception.
 */
static void (*const expected_stubs[32])(void) = {
#define STUB(n) exception_stub_##n,
    STUB(0)  STUB(1)  STUB(2)  STUB(3)
    STUB(4)  STUB(5)  STUB(6)  STUB(7)
    STUB(8)  STUB(9)  STUB(10) STUB(11)
    STUB(12) STUB(13) STUB(14) STUB(15)
    STUB(16) STUB(17) STUB(18) STUB(19)
    STUB(20) STUB(21) STUB(22) STUB(23)
    STUB(24) STUB(25) STUB(26) STUB(27)
    STUB(28) STUB(29) STUB(30) STUB(31)
#undef STUB
};

_Static_assert(
    sizeof(expected_stubs) / sizeof(expected_stubs[0]) == 32,
    "Exactly 32 exception handlers required"
);

static uint64_t decode_handler(
    const struct idt_entry *entry
)
{
    return
        (uint64_t)entry->offset_low |
        ((uint64_t)entry->offset_middle << 16) |
        ((uint64_t)entry->offset_high << 32);
}

int exception_gate_identity_check(
    const struct idt_table *table
)
{
    if (table == NULL) {
        return 0;
    }

    for (unsigned int vector = 0; vector < 32; vector++) {
        uint64_t actual =
            decode_handler(&table->entries[vector]);

        uint64_t expected =
            (uint64_t)(uintptr_t)expected_stubs[vector];

        if (actual != expected) {
            return 0;
        }
    }

    return 1;
}
