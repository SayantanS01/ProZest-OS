#include <stdint.h>

#include "console.h"
#include "cpu_state.h"
#include "cpu_diagnostics.h"

/*
 * Convert an unsigned number to a
 * hexadecimal string.
 *
 * digits is 4 or 16 in this module.
 *
 * No malloc, printf or standard C
 * runtime is required.
 */
static void cpu_diag_write_hex(
    uint64_t value,
    unsigned int digits
)
{
    static const char hex[] = "0123456789ABCDEF";

    char text[19];

    text[0] = '0';
    text[1] = 'x';

    for (unsigned int i = 0; i < digits; i++) {
        unsigned int shift =
            (digits - 1u - i) * 4u;

        unsigned int index =
            (unsigned int)((value >> shift) & 0xFu);

        text[2u + i] = hex[index];
    }

    text[2u + digits] = '\0';

    console_write(text);
}

/*
 * Display a label and a hexadecimal
 * register value on one console line.
 */
static void cpu_diag_write_register(
    const char *label,
    uint64_t value,
    unsigned int digits
)
{
    console_write(label);
    cpu_diag_write_hex(value, digits);
    console_newline();
}

/*
 * Display the actual descriptor-table
 * state of the executing CPU.
 */
void cpu_diagnostics_show(void)
{
    struct cpu_state_snapshot snapshot = {0};

    /*
     * Read the processor's registers.
     *
     * No LGDT, LIDT, or LTR is executed.
     */
    cpu_state_capture(&snapshot);

    console_newline();

    console_write("CPU DESCRIPTOR STATE");
    console_newline();

    console_write("--------------------");
    console_newline();

    /* Segment selectors. */
    cpu_diag_write_register("CS: ", snapshot.cs, 4);
    cpu_diag_write_register("SS: ", snapshot.ss, 4);
    cpu_diag_write_register("DS: ", snapshot.ds, 4);
    cpu_diag_write_register("ES: ", snapshot.es, 4);
    cpu_diag_write_register("TR: ", snapshot.tr, 4);

    /* Global Descriptor Table Register. */
    cpu_diag_write_register(
        "GDTR LIMIT: ",
        snapshot.gdtr.limit,
        4
    );

    cpu_diag_write_register(
        "GDTR BASE: ",
        snapshot.gdtr.base,
        16
    );

    /* Interrupt Descriptor Table Register. */
    cpu_diag_write_register(
        "IDTR LIMIT: ",
        snapshot.idtr.limit,
        4
    );

    cpu_diag_write_register(
        "IDTR BASE: ",
        snapshot.idtr.base,
        16
    );
}
