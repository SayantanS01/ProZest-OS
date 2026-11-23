#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "tss.h"

#define GUARD_SIZE 32u
#define GUARD_VALUE 0xA5u

static int failures = 0;

static void check(const char *name, int passed)
{
    printf("[%s] %s\n",
           passed ? "PASS" : "FAIL",
           name);

    if (!passed) {
        failures++;
    }
}

/*
 * Verify that two 16 KiB stack regions
 * do not overlap, assuming their addresses
 * describe the high ends of the regions.
 */
static int stacks_separate(uint64_t a, uint64_t b)
{
    uint64_t distance =
        a > b ? a - b : b - a;

    return distance >= TSS_IST_STACK_SIZE;
}

/*
 * Host-side memory accessibility check.
 *
 * Test the lowest and highest allocated bytes.
 * Restore both bytes after the test.
 *
 * These are not privileged CPU instructions.
 */
static int check_stack_storage(uint64_t top_address)
{
    if (top_address == 0 ||
        top_address < TSS_IST_STACK_SIZE) {
        return 0;
    }

    volatile uint8_t *top =
        (volatile uint8_t *)(uintptr_t)top_address;

    volatile uint8_t *bottom =
        top - TSS_IST_STACK_SIZE;

    uint8_t original_bottom = *bottom;
    uint8_t original_top = *(top - 1);

    *bottom = 0x71u;
    *(top - 1) = 0xACu;

    int passed =
        *bottom == 0x71u &&
        *(top - 1) == 0xACu;

    *bottom = original_bottom;
    *(top - 1) = original_top;

    return passed;
}

int main(void)
{
    printf("ProZest OS - TSS Stack Tests\n");
    printf("===========================\n");

    /*
     * Place guard bytes around the TSS.
     * They should remain unchanged.
     */
    struct {
        uint8_t before[GUARD_SIZE];
        struct tss64 tss;
        uint8_t after[GUARD_SIZE];
    } protected_tss;

    memset(
        &protected_tss,
        GUARD_VALUE,
        sizeof(protected_tss)
    );

    tss_prepare_bootstrap(&protected_tss.tss);

    struct tss64 *tss = &protected_tss.tss;

    check(
        "TSS structure size",
        sizeof(*tss) == 104
    );

    check(
        "Emergency stack size",
        TSS_IST_STACK_SIZE == 16384u
    );

    check(
        "IST indices",
        TSS_IST_DOUBLE_FAULT == 1u &&
        TSS_IST_NMI == 2u &&
        TSS_IST_MACHINE_CHECK == 3u
    );

    check(
        "I/O bitmap disabled",
        tss->iomap_base == sizeof(*tss)
    );

    check(
        "Privilege stack pointers cleared",
        tss->rsp[0] == 0 &&
        tss->rsp[1] == 0 &&
        tss->rsp[2] == 0
    );

    check(
        "Reserved TSS fields cleared",
        tss->reserved0 == 0 &&
        tss->reserved1 == 0 &&
        tss->reserved2 == 0 &&
        tss->reserved3 == 0
    );

    uint64_t df = tss->ist[0];
    uint64_t nmi = tss->ist[1];
    uint64_t mc = tss->ist[2];

    check("Double-fault stack assigned", df != 0);
    check("NMI stack assigned", nmi != 0);
    check("Machine-check stack assigned", mc != 0);

    int unused_clear = 1;

    for (size_t i = 3; i < TSS_IST_COUNT; i++) {
        if (tss->ist[i] != 0) {
            unused_clear = 0;
        }
    }

    check("Unused IST slots cleared", unused_clear);

    check(
        "Stack tops are 16-byte aligned",
        (df % 16u) == 0 &&
        (nmi % 16u) == 0 &&
        (mc % 16u) == 0
    );

    check(
        "Emergency stacks do not overlap",
        stacks_separate(df, nmi) &&
        stacks_separate(df, mc) &&
        stacks_separate(nmi, mc)
    );

    check(
        "Double-fault stack memory accessible",
        check_stack_storage(df)
    );

    check(
        "NMI stack memory accessible",
        check_stack_storage(nmi)
    );

    check(
        "Machine-check stack memory accessible",
        check_stack_storage(mc)
    );

    int guards_ok = 1;

    for (size_t i = 0; i < GUARD_SIZE; i++) {
        if (protected_tss.before[i] != GUARD_VALUE ||
            protected_tss.after[i] != GUARD_VALUE) {
            guards_ok = 0;
        }
    }

    check("TSS guard bytes unchanged", guards_ok);

    struct tss64 snapshot = *tss;

    tss_prepare_bootstrap(tss);

    check(
        "Repeated initialization is consistent",
        memcmp(&snapshot, tss, sizeof(*tss)) == 0
    );

    /*
     * NULL must be ignored without dereferencing it.
     */
    tss_prepare_bootstrap(NULL);
    check("NULL TSS call returns normally", 1);

    printf("---------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
