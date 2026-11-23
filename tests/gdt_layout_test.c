#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "gdt.h"

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
    printf("ProZest OS - GDT Layout Tests\n");
    printf("============================\n");

    check("GDT size",
          sizeof(struct gdt_table), 40);

    check("TSS descriptor size",
          sizeof(struct gdt_tss_descriptor), 16);

    check("GDTR operand size",
          sizeof(struct gdt_pointer), 10);

    check("Null descriptor offset",
          offsetof(struct gdt_table,
                   null_descriptor), 0);

    check("Kernel code offset",
          offsetof(struct gdt_table,
                   kernel_code_descriptor), 8);

    check("Kernel data offset",
          offsetof(struct gdt_table,
                   kernel_data_descriptor), 16);

    check("TSS descriptor offset",
          offsetof(struct gdt_table,
                   tss_descriptor), 24);

    check("TSS base-low offset",
          offsetof(struct gdt_tss_descriptor,
                   base_low), 2);

    check("TSS base-middle offset",
          offsetof(struct gdt_tss_descriptor,
                   base_middle), 4);

    check("TSS access offset",
          offsetof(struct gdt_tss_descriptor,
                   access), 5);

    check("TSS flags offset",
          offsetof(struct gdt_tss_descriptor,
                   limit_high_flags), 6);

    check("TSS base-high offset",
          offsetof(struct gdt_tss_descriptor,
                   base_high), 7);

    check("TSS upper-base offset",
          offsetof(struct gdt_tss_descriptor,
                   base_upper), 8);

    check("TSS reserved offset",
          offsetof(struct gdt_tss_descriptor,
                   reserved), 12);

    check("GDTR base offset",
          offsetof(struct gdt_pointer,
                   base), 2);

    check("Kernel code selector",
          GDT_KERNEL_CODE_SELECTOR, 0x08);

    check("Kernel data selector",
          GDT_KERNEL_DATA_SELECTOR, 0x10);

    check("TSS selector",
          GDT_TSS_SELECTOR, 0x18);

    check("TSS access byte",
          GDT_TSS_ACCESS, 0x89);

    check("GDT alignment",
          _Alignof(struct gdt_table), 8);

    printf("----------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
