#include <stddef.h>
#include <stdint.h>

#include "idt_exceptions.h"
#include "gdt.h"
#include "tss.h"

/*
 * Declare the 32 assembly exception
 * entry points implemented in:
 *
 * kernel/arch/x86_64/exception_stubs.S
 */
#define DECLARE_STUB(n) extern void exception_stub_##n(void);

DECLARE_STUB(0)
DECLARE_STUB(1)
DECLARE_STUB(2)
DECLARE_STUB(3)
DECLARE_STUB(4)
DECLARE_STUB(5)
DECLARE_STUB(6)
DECLARE_STUB(7)
DECLARE_STUB(8)
DECLARE_STUB(9)
DECLARE_STUB(10)
DECLARE_STUB(11)
DECLARE_STUB(12)
DECLARE_STUB(13)
DECLARE_STUB(14)
DECLARE_STUB(15)
DECLARE_STUB(16)
DECLARE_STUB(17)
DECLARE_STUB(18)
DECLARE_STUB(19)
DECLARE_STUB(20)
DECLARE_STUB(21)
DECLARE_STUB(22)
DECLARE_STUB(23)
DECLARE_STUB(24)
DECLARE_STUB(25)
DECLARE_STUB(26)
DECLARE_STUB(27)
DECLARE_STUB(28)
DECLARE_STUB(29)
DECLARE_STUB(30)
DECLARE_STUB(31)

#undef DECLARE_STUB

/*
 * Indexed table of handler function pointers.
 *
 * Entry n corresponds to CPU exception n.
 */
static void (*const exception_stubs[32])(void) = {
#define STUB(n) exception_stub_##n,
    STUB(0)
    STUB(1)
    STUB(2)
    STUB(3)
    STUB(4)
    STUB(5)
    STUB(6)
    STUB(7)
    STUB(8)
    STUB(9)
    STUB(10)
    STUB(11)
    STUB(12)
    STUB(13)
    STUB(14)
    STUB(15)
    STUB(16)
    STUB(17)
    STUB(18)
    STUB(19)
    STUB(20)
    STUB(21)
    STUB(22)
    STUB(23)
    STUB(24)
    STUB(25)
    STUB(26)
    STUB(27)
    STUB(28)
    STUB(29)
    STUB(30)
    STUB(31)
#undef STUB
};

_Static_assert(
    sizeof(exception_stubs) / sizeof(exception_stubs[0]) == 32,
    "Exactly 32 exception stubs required"
);

/*
 * Select the emergency stack for
 * a small set of critical exceptions.
 */
static uint8_t exception_ist_index(uint16_t vector)
{
    switch (vector) {
#ifdef PROZEST_IST4_TEST
        case 6: return 4;
#endif
        case 2:
            return TSS_IST_NMI;

        case 8:
            return TSS_IST_DOUBLE_FAULT;

        case 18:
            return TSS_IST_MACHINE_CHECK;

        default:
            return 0;
    }
}

/*
 * Build all 32 exception gates.
 *
 * No privileged instructions are executed.
 */
int idt_exceptions_prepare(struct idt_table *table)
{
    if (table == NULL) {
        return 0;
    }

    idt_table_init(table);

    for (uint16_t vector = 0; vector < 32; vector++) {
        uint64_t handler_address =
            (uint64_t)(uintptr_t)exception_stubs[vector];

        if (!idt_table_set_gate(
                table,
                vector,
                handler_address,
                GDT_KERNEL_CODE_SELECTOR,
                exception_ist_index(vector),
                IDT_INTERRUPT_GATE
            )) {
            return 0;
        }
    }

    return 1;
}
