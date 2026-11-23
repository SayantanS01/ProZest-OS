#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

#include "gdt.h"
#include "tss.h"

/*
 * Persistent bootstrap CPU structures.
 *
 * This is host-side testing only.
 * The kernel will eventually need its own
 * persistent versions of these objects.
 */
static struct tss64 bootstrap_tss
    __attribute__((aligned(16)));

static struct gdt_table bootstrap_gdt;

static struct gdt_pointer bootstrap_gdtr;

static int checks = 0;
static int failures = 0;

static void check(const char *name, int passed)
{
    checks++;

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
 * Reconstruct the 64-bit TSS address
 * from the GDT system descriptor.
 */
static uint64_t decode_tss_base(
    const struct gdt_tss_descriptor *d
)
{
    return
        (uint64_t)d->base_low |
        ((uint64_t)d->base_middle << 16) |
        ((uint64_t)d->base_high << 24) |
        ((uint64_t)d->base_upper << 32);
}

/*
 * Compare two downward-growing stack regions.
 *
 * Each pointer identifies the high end
 * of a 16 KiB region.
 */
static int stacks_separate(uint64_t a, uint64_t b)
{
    uint64_t distance =
        a > b ? a - b : b - a;

    return distance >= TSS_IST_STACK_SIZE;
}

int main(void)
{
    printf("ProZest OS - GDT/TSS Integration Tests\n");
    printf("=====================================\n");

    /*
     * Initialize the Task State Segment.
     */
    tss_prepare_bootstrap(&bootstrap_tss);

    check(
        "TSS I/O bitmap offset",
        bootstrap_tss.iomap_base == 104u
    );

    check(
        "Privilege stack pointers zero",
        bootstrap_tss.rsp[0] == 0 &&
        bootstrap_tss.rsp[1] == 0 &&
        bootstrap_tss.rsp[2] == 0
    );

    uint64_t df = bootstrap_tss.ist[0];
    uint64_t nmi = bootstrap_tss.ist[1];
    uint64_t mc = bootstrap_tss.ist[2];

    check("Double-fault IST assigned", df != 0);
    check("NMI IST assigned", nmi != 0);
    check("Machine-check IST assigned", mc != 0);

    check(
        "All emergency stacks aligned",
        (df % 16u) == 0 &&
        (nmi % 16u) == 0 &&
        (mc % 16u) == 0
    );

    check(
        "Emergency stack regions separate",
        stacks_separate(df, nmi) &&
        stacks_separate(df, mc) &&
        stacks_separate(nmi, mc)
    );

    int unused_clear = 1;

    for (size_t i = 3; i < TSS_IST_COUNT; i++) {
        if (bootstrap_tss.ist[i] != 0) {
            unused_clear = 0;
        }
    }

    check("Unused IST slots zero", unused_clear);

    check(
        "TSS object 16-byte aligned",
        ((uintptr_t)&bootstrap_tss % 16u) == 0
    );

    /*
     * Construct the GDT using the actual TSS.
     */
    check(
        "GDT initialization succeeds",
        gdt_table_init(
            &bootstrap_gdt,
            &bootstrap_tss
        ) == 1
    );

    check(
        "Null descriptor is zero",
        bootstrap_gdt.null_descriptor == 0
    );

    check(
        "Kernel code descriptor correct",
        bootstrap_gdt.kernel_code_descriptor ==
            GDT_KERNEL_CODE_DESCRIPTOR
    );

    check(
        "Kernel data descriptor correct",
        bootstrap_gdt.kernel_data_descriptor ==
            GDT_KERNEL_DATA_DESCRIPTOR
    );

    check(
        "TSS access byte correct",
        bootstrap_gdt.tss_descriptor.access == 0x89u
    );

    check(
        "TSS limit is 103",
        bootstrap_gdt.tss_descriptor.limit_low == 103u &&
        bootstrap_gdt.tss_descriptor.limit_high_flags == 0
    );

    check(
        "TSS descriptor points to actual TSS",
        decode_tss_base(
            &bootstrap_gdt.tss_descriptor
        ) == (uint64_t)(uintptr_t)&bootstrap_tss
    );

    check(
        "Reserved TSS descriptor field zero",
        bootstrap_gdt.tss_descriptor.reserved == 0
    );

    check(
        "GDT selectors match descriptor offsets",
        GDT_KERNEL_CODE_SELECTOR ==
            offsetof(struct gdt_table,
                     kernel_code_descriptor) &&
        GDT_KERNEL_DATA_SELECTOR ==
            offsetof(struct gdt_table,
                     kernel_data_descriptor) &&
        GDT_TSS_SELECTOR ==
            offsetof(struct gdt_table,
                     tss_descriptor)
    );

    check(
        "GDT object 8-byte aligned",
        ((uintptr_t)&bootstrap_gdt % 8u) == 0
    );

    /*
     * Construct the GDTR operand.
     */
    check(
        "GDTR initialization succeeds",
        gdt_pointer_init(
            &bootstrap_gdtr,
            &bootstrap_gdt
        ) == 1
    );

    check(
        "GDTR limit equals 39",
        bootstrap_gdtr.limit == 39u
    );

    check(
        "GDTR points to actual GDT",
        bootstrap_gdtr.base ==
            (uint64_t)(uintptr_t)&bootstrap_gdt
    );

    /*
     * Reinitializing must preserve the
     * correct configuration.
     */
    struct tss64 previous_tss = bootstrap_tss;
    struct gdt_table previous_gdt = bootstrap_gdt;

    tss_prepare_bootstrap(&bootstrap_tss);

    int repeated_ok =
        gdt_table_init(
            &bootstrap_gdt,
            &bootstrap_tss
        ) == 1;

    check(
        "Repeated initialization consistent",
        repeated_ok &&
        memcmp(
            &previous_tss,
            &bootstrap_tss,
            sizeof(bootstrap_tss)
        ) == 0 &&
        memcmp(
            &previous_gdt,
            &bootstrap_gdt,
            sizeof(bootstrap_gdt)
        ) == 0
    );

    printf("-------------------------------------\n");

    printf(
        "Host TSS address: 0x%016" PRIx64 "\n",
        (uint64_t)(uintptr_t)&bootstrap_tss
    );

    printf(
        "Host GDT address: 0x%016" PRIx64 "\n",
        (uint64_t)(uintptr_t)&bootstrap_gdt
    );

    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
