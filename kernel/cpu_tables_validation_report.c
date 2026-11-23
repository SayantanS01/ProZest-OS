#include <stddef.h>

#include "cpu_tables_validation_report.h"
#include "bootstrap_tables.h"
#include "bootstrap_idt.h"
#include "cpu_tables_preflight.h"
#include "exception_gate_identity.h"
#include "console.h"

/*
 * Print one result using the existing
 * ProZest OS graphical console.
 */
static void print_result(
    const char *description,
    int passed
)
{
    console_write(
        passed ? "[PASS] " : "[FAIL] "
    );

    console_write(description);
    console_newline();
}

/*
 * Inspect existing prepared CPU tables.
 *
 * This function does not initialize tables
 * again and does not modify CPU registers.
 */
int cpu_tables_validation_report_run(void)
{
    console_newline();

    console_write("M09 DESCRIPTOR VALIDATION");
    console_newline();

    const struct bootstrap_cpu_tables *cpu =
        bootstrap_cpu_tables_get();

    const struct bootstrap_idt_state *irq =
        bootstrap_idt_get();

    /*
     * Do not dereference missing tables.
     */
    if (cpu == NULL || irq == NULL) {
        print_result(
            "Prepared CPU tables available",
            0
        );

        console_write(
            "M09 VALIDATION: FAIL"
        );
        console_newline();

        return 0;
    }

    /*
     * Layer 1:
     * Validate descriptor structure.
     */
    int structure_ok =
        cpu_tables_preflight_check(
            cpu,
            irq
        );

    print_result(
        "Descriptor structure valid",
        structure_ok
    );

    /*
     * Layer 2:
     * Verify all 32 actual assembly
     * handler addresses.
     */
    int handlers_ok =
        exception_gate_identity_check(
            &irq->table
        );

    print_result(
        "32 handler identities valid",
        handlers_ok
    );

    int success =
        structure_ok && handlers_ok;

    console_write(
        success
        ? "M09 VALIDATION: PASS"
        : "M09 VALIDATION: FAIL"
    );

    console_newline();

    return success;
}
