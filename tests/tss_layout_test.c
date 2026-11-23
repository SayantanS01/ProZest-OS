#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "tss.h"

static int failures = 0;

static void check(
    const char *name,
    size_t actual,
    size_t expected
)
{
    int passed = actual == expected;

    printf(
        "[%s] %s: %zu\n",
        passed ? "PASS" : "FAIL",
        name,
        actual
    );

    if (!passed) {
        printf("  Expected: %zu\n", expected);
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - TSS Layout Tests\n");
    printf("============================\n");

    check(
        "TSS size",
        sizeof(struct tss64),
        104
    );

    check(
        "Reserved0 offset",
        offsetof(struct tss64, reserved0),
        0
    );

    check(
        "RSP0 offset",
        offsetof(struct tss64, rsp[0]),
        4
    );

    check(
        "RSP1 offset",
        offsetof(struct tss64, rsp[1]),
        12
    );

    check(
        "RSP2 offset",
        offsetof(struct tss64, rsp[2]),
        20
    );

    check(
        "IST1 offset",
        offsetof(struct tss64, ist[0]),
        36
    );

    check(
        "IST2 offset",
        offsetof(struct tss64, ist[1]),
        44
    );

    check(
        "IST7 offset",
        offsetof(struct tss64, ist[6]),
        84
    );

    check(
        "I/O map offset",
        offsetof(struct tss64, iomap_base),
        102
    );

    check(
        "IST slot count",
        TSS_IST_COUNT,
        7
    );

    /*
     * Zero initialization does not yet
     * configure usable stacks.
     */
    struct tss64 tss = {0};

    check(
        "RSP0 initially zero",
        (size_t)tss.rsp[0],
        0
    );

    check(
        "IST1 initially zero",
        (size_t)tss.ist[0],
        0
    );

    /*
     * No I/O permission bitmap is present.
     * Its offset will be beyond the TSS limit.
     */
    tss.iomap_base = sizeof(struct tss64);

    check(
        "I/O bitmap disabled",
        tss.iomap_base,
        104
    );

    printf("----------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
