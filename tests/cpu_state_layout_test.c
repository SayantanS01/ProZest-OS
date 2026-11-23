#include <stdio.h>
#include <stddef.h>

#include "cpu_state.h"

static int checks = 0;
static int failures = 0;

static void check(
    const char *name,
    size_t actual,
    size_t expected
)
{
    checks++;

    int passed = actual == expected;

    printf(
        "[%s] %s: %zu\n",
        passed ? "PASS" : "FAIL",
        name,
        actual
    );

    if (!passed) {
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - CPU State Layout Tests\n");
    printf("==================================\n");

    check("GDTR size",
          sizeof(struct gdt_pointer), 10);

    check("IDTR size",
          sizeof(struct idt_pointer), 10);

    check("Snapshot size",
          sizeof(struct cpu_state_snapshot), 30);

    check("IDTR offset",
          offsetof(struct cpu_state_snapshot, idtr), 10);

    check("CS offset",
          offsetof(struct cpu_state_snapshot, cs), 20);

    check("SS offset",
          offsetof(struct cpu_state_snapshot, ss), 22);

    check("DS offset",
          offsetof(struct cpu_state_snapshot, ds), 24);

    check("ES offset",
          offsetof(struct cpu_state_snapshot, es), 26);

    check("TR offset",
          offsetof(struct cpu_state_snapshot, tr), 28);

    printf("----------------------------------\n");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
