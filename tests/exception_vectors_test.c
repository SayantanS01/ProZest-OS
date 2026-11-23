#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "exception.h"
#include "console.h"
#include "logger.h"

/*
 * Host-side mock dependencies.
 *
 * exception_dispatch() is not executed.
 */
void console_write(const char *text)
{
    (void)text;
}

void console_newline(void)
{
}

void log_write(
    log_level_t level,
    const char *message
)
{
    (void)level;
    (void)message;
}

static const char *const expected_names[32] = {
    "Division error (#DE)",
    "Debug exception (#DB)",
    "Non-maskable interrupt (NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND range exceeded (#BR)",
    "Invalid opcode (#UD)",
    "Device not available (#NM)",
    "Double fault (#DF)",
    "Reserved CPU exception vector",
    "Invalid TSS (#TS)",
    "Segment not present (#NP)",
    "Stack-segment fault (#SS)",
    "General protection (#GP)",
    "Page fault (#PF)",
    "Reserved CPU exception vector",
    "x87 floating-point (#MF)",
    "Alignment check (#AC)",
    "Machine check (#MC)",
    "SIMD floating-point (#XM)",
    "Virtualization exception (#VE)",
    "Control protection (#CP)",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Hypervisor injection (#HV)",
    "VMM communication (#VC)",
    "Security exception (#SX)",
    "Reserved CPU exception vector"
};

static const int expected_error_codes[32] = {
    [8] = 1,
    [10] = 1,
    [11] = 1,
    [12] = 1,
    [13] = 1,
    [14] = 1,
    [17] = 1,
    [21] = 1,
    [29] = 1,
    [30] = 1
};

int main(void)
{
    int failures = 0;

    printf("ProZest OS - Full Exception Vector Tests\n");
    printf("=======================================\n");

    for (uint64_t vector = 0; vector < 32; vector++) {
        const char *name = exception_name(vector);

        int name_ok =
            name != NULL &&
            strcmp(
                name,
                expected_names[vector]
            ) == 0;

        int error_ok =
            exception_has_error_code(vector) ==
                expected_error_codes[vector];

        int passed = name_ok && error_ok;

        printf(
            "[%s] Vector %02llu - %s\n",
            passed ? "PASS" : "FAIL",
            (unsigned long long)vector,
            name != NULL ? name : "(null)"
        );

        if (!passed) {
            failures++;

            if (!name_ok) {
                printf("  Incorrect exception name\n");
            }

            if (!error_ok) {
                printf("  Incorrect error-code policy\n");
            }
        }
    }

    const char *unknown =
        "Unknown CPU exception";

    int invalid_32 =
        strcmp(exception_name(32), unknown) == 0 &&
        exception_has_error_code(32) == 0;

    printf(
        "[%s] Reject vector 32\n",
        invalid_32 ? "PASS" : "FAIL"
    );

    if (!invalid_32) {
        failures++;
    }

    int invalid_max =
        strcmp(exception_name(UINT64_MAX), unknown) == 0 &&
        exception_has_error_code(UINT64_MAX) == 0;

    printf(
        "[%s] Reject UINT64_MAX vector\n",
        invalid_max ? "PASS" : "FAIL"
    );

    if (!invalid_max) {
        failures++;
    }

    printf("---------------------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
