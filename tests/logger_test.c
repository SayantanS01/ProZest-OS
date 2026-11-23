#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "logger.h"

/*
 * Mock console output buffer.
 *
 * The real kernel console is not linked
 * into this host-side unit test.
 */
#define OUTPUT_CAPACITY 1024

static char output[OUTPUT_CAPACITY];
static size_t output_length = 0;
static int failures = 0;

/* Capture output instead of drawing pixels. */
void console_write(const char *text)
{
    if (text == NULL) {
        return;
    }

    while (*text != '\0' &&
           output_length < OUTPUT_CAPACITY - 1) {
        output[output_length++] = *text++;
    }

    output[output_length] = '\0';
}

/* Simulate console newline handling. */
void console_newline(void)
{
    console_write("\n");
}

/* Clear captured output before each test. */
static void reset_output(void)
{
    output_length = 0;
    output[0] = '\0';
}

/* Compare captured output with expectation. */
static void expect_output(
    const char *test_name,
    const char *expected
)
{
    if (strcmp(output, expected) == 0) {
        printf("[PASS] %s\n", test_name);
    } else {
        printf("[FAIL] %s\n", test_name);
        printf("  Expected: %s", expected);
        printf("  Actual:   %s", output);
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - Kernel Logger Tests\n");
    printf("===============================\n");

    reset_output();
    log_write(LOG_DEBUG, "Debug message");
    expect_output(
        "DEBUG prefix",
        "[DEBUG] Debug message\n"
    );

    reset_output();
    log_write(LOG_INFO, "Kernel started");
    expect_output(
        "INFO prefix",
        "[INFO]  Kernel started\n"
    );

    reset_output();
    log_write(LOG_OK, "Console ready");
    expect_output(
        "OK prefix",
        "[ OK ]  Console ready\n"
    );

    reset_output();
    log_write(LOG_WARN, "Example warning");
    expect_output(
        "WARN prefix",
        "[WARN]  Example warning\n"
    );

    reset_output();
    log_write(LOG_ERROR, "Example error");
    expect_output(
        "ERROR prefix",
        "[ERROR] Example error\n"
    );

    reset_output();
    log_write((log_level_t)99, "Unknown level");
    expect_output(
        "Unknown severity fallback",
        "[????]  Unknown level\n"
    );

    reset_output();
    log_write(LOG_INFO, NULL);
    expect_output(
        "NULL message ignored",
        ""
    );

    reset_output();
    log_write(LOG_OK, "");
    expect_output(
        "Empty message",
        "[ OK ]  \n"
    );

    printf("-------------------------------\n");
    printf("Failures: %d\n", failures);

    if (failures == 0) {
        printf("[PASS] All logger tests\n");
        return 0;
    }

    return 1;
}
