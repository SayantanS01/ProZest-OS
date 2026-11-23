#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "gdt.h"
#include "tss.h"

static int failures = 0;
static int checks = 0;

static void check(const char *name, int passed)
{
    checks++;

    printf("[%s] %s\n",
           passed ? "PASS" : "FAIL",
           name);

    if (!passed) {
        failures++;
    }
}

/*
 * Reconstruct the 64-bit base address
 * from the split descriptor fields.
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

int main(void)
{
    printf("ProZest OS - GDT Initialization Tests\n");
    printf("====================================\n");

    struct tss64 tss = {0};
    struct gdt_table table;
    struct gdt_pointer gdtr;

    memset(&table, 0xA5, sizeof(table));

    check(
        "GDT initialization succeeds",
        gdt_table_init(&table, &tss) == 1
    );

    check(
        "Null descriptor is zero",
        table.null_descriptor == 0
    );

    check(
        "Kernel code descriptor",
        table.kernel_code_descriptor ==
            GDT_KERNEL_CODE_DESCRIPTOR
    );

    check(
        "Kernel data descriptor",
        table.kernel_data_descriptor ==
            GDT_KERNEL_DATA_DESCRIPTOR
    );

    /*
     * Code descriptor must be a present
     * ring-0 64-bit executable segment.
     */
    uint64_t code =
        table.kernel_code_descriptor;

    check(
        "64-bit code flag enabled",
        ((code >> 53) & 1u) == 1u
    );

    check(
        "32-bit code flag disabled",
        ((code >> 54) & 1u) == 0u
    );

    check(
        "Code segment present",
        ((code >> 47) & 1u) == 1u
    );

    check(
        "Code segment privilege level zero",
        ((code >> 45) & 3u) == 0u
    );

    check(
        "TSS access byte correct",
        table.tss_descriptor.access == 0x89u
    );

    check(
        "TSS descriptor limit correct",
        table.tss_descriptor.limit_low == 103u &&
        table.tss_descriptor.limit_high_flags == 0u
    );

    check(
        "TSS descriptor points to real TSS",
        decode_tss_base(&table.tss_descriptor) ==
            (uint64_t)(uintptr_t)&tss
    );

    check(
        "TSS reserved descriptor field cleared",
        table.tss_descriptor.reserved == 0
    );

    check(
        "GDTR initialization succeeds",
        gdt_pointer_init(&gdtr, &table) == 1
    );

    check(
        "GDTR limit is 39",
        gdtr.limit == 39u
    );

    check(
        "GDTR base points to GDT",
        gdtr.base ==
            (uint64_t)(uintptr_t)&table
    );

    struct gdt_table snapshot = table;

    gdt_table_init(&table, &tss);

    check(
        "Repeated GDT initialization is consistent",
        memcmp(&table, &snapshot,
               sizeof(table)) == 0
    );

    check(
        "NULL GDT rejected",
        gdt_table_init(NULL, &tss) == 0
    );

    check(
        "NULL TSS rejected",
        gdt_table_init(&table, NULL) == 0
    );

    check(
        "Rejected initialization leaves GDT unchanged",
        memcmp(&table, &snapshot,
               sizeof(table)) == 0
    );

    check(
        "NULL GDTR output rejected",
        gdt_pointer_init(NULL, &table) == 0
    );

    check(
        "NULL GDTR input rejected",
        gdt_pointer_init(&gdtr, NULL) == 0
    );

    printf("------------------------------------\n");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
