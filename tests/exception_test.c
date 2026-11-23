#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "exception.h"
#include "console.h"
#include "logger.h"

/*
 * Mock dependencies for exception_dispatch().
 *
 * These functions exist only so the real
 * exception.c can link as a host-side test.
 *
 * We will NOT call exception_dispatch().
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

static int failures = 0;

static void check(
    const char *name,
    int passed
)
{
    printf(
        "[%s] %s\n",
        passed ? "PASS" : "FAIL",
        name
    );

    if (!passed) {
        failures++;
    }
}

/*
 * Verify the readable exception name.
 */
static void check_name(
    const char *test_name,
    uint64_t vector,
    const char *expected
)
{
    const char *actual = exception_name(vector);

    int passed =
        actual != NULL &&
        strcmp(actual, expected) == 0;

    check(test_name, passed);

    if (!passed && actual != NULL) {
        printf("  Actual: %s\n", actual);
    }
}

/*
 * Verify formatting and detect writes
 * beyond the required 19-byte buffer.
 */
static void check_hex(
    const char *test_name,
    uint64_t value,
    const char *expected
)
{
    char buffer[EXCEPTION_HEX_BUFFER_SIZE + 2];

    memset(buffer, 'X', sizeof(buffer));

    exception_format_hex(value, buffer);

    int correct_text =
        strcmp(buffer, expected) == 0;

    int guard_unchanged =
        buffer[EXCEPTION_HEX_BUFFER_SIZE] == 'X' &&
        buffer[EXCEPTION_HEX_BUFFER_SIZE + 1] == 'X';

    check(
        test_name,
        correct_text && guard_unchanged
    );

    if (!correct_text) {
        printf("  Expected: %s\n", expected);
        printf("  Actual:   %s\n", buffer);
    }

    if (!guard_unchanged) {
        printf("  Output buffer guard modified\n");
    }
}

int main(void)
{
    printf("ProZest OS - Exception Diagnostic Tests\n");
    printf("======================================\n");

    check_name(
        "Division error name",
        0,
        "Division error (#DE)"
    );

    check_name(
        "Invalid opcode name",
        6,
        "Invalid opcode (#UD)"
    );

    check_name(
        "General protection name",
        13,
        "General protection (#GP)"
    );

    check_name(
        "Page fault name",
        14,
        "Page fault (#PF)"
    );

    check_name(
        "Unknown vector fallback",
        255,
        "Unknown CPU exception"
    );

    check_hex(
        "Zero in hexadecimal",
        0,
        "0x0000000000000000"
    );

    check_hex(
        "Page fault vector hexadecimal",
        14,
        "0x000000000000000E"
    );

    check_hex(
        "Full 64-bit hexadecimal",
        UINT64_MAX,
        "0xFFFFFFFFFFFFFFFF"
    );

    check_hex(
        "Mixed hexadecimal digits",
        0x123456789ABCDEF0ULL,
        "0x123456789ABCDEF0"
    );

    check_hex(
        "Highest bit set",
        0x8000000000000000ULL,
        "0x8000000000000000"
    );

    /*
     * NULL output must be ignored.
     *
     * This verifies a normal return,
     * not arbitrary pointer safety.
     */
    exception_format_hex(123, NULL);

    check(
        "NULL output ignored",
        1
    );

    printf("--------------------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
