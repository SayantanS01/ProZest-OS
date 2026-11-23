#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "bootstrap_tables.h"

static int checks = 0;
static int failures = 0;

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

static uint64_t decode_tss_base(
    const struct gdt_tss_descriptor *d)
{
    return (uint64_t)d->base_low |
           ((uint64_t)d->base_middle << 16) |
           ((uint64_t)d->base_high << 24) |
           ((uint64_t)d->base_upper << 32);
}

static int stacks_separate(uint64_t a, uint64_t b)
{
    uint64_t distance = a > b ? a - b : b - a;
    return distance >= TSS_IST_STACK_SIZE;
}

int main(void)
{
    puts("ProZest OS - Persistent CPU Tables");
    puts("=================================");

    check("Unavailable before preparation",
          bootstrap_cpu_tables_get() == NULL);

    check("Preparation succeeds",
          bootstrap_cpu_tables_prepare() == 1);

    const struct bootstrap_cpu_tables *state =
        bootstrap_cpu_tables_get();

    check("Prepared state available", state != NULL);

    if (state == NULL) {
        return 1;
    }

    check("Getter returns stable address",
          bootstrap_cpu_tables_get() == state);

    check("TSS size", sizeof(state->tss) == 104);
    check("GDT size", sizeof(state->gdt) == 40);
    check("GDTR size", sizeof(state->gdtr) == 10);

    check("TSS 16-byte alignment",
          ((uintptr_t)&state->tss % 16) == 0);

    check("GDT 8-byte alignment",
          ((uintptr_t)&state->gdt % 8) == 0);

    check("GDTR limit equals 39",
          state->gdtr.limit == 39);

    check("GDTR points to persistent GDT",
          state->gdtr.base ==
          (uint64_t)(uintptr_t)&state->gdt);

    check("Null descriptor cleared",
          state->gdt.null_descriptor == 0);

    check("Kernel code descriptor",
          state->gdt.kernel_code_descriptor ==
          GDT_KERNEL_CODE_DESCRIPTOR);

    check("Kernel data descriptor",
          state->gdt.kernel_data_descriptor ==
          GDT_KERNEL_DATA_DESCRIPTOR);

    check("Selector offsets",
          GDT_KERNEL_CODE_SELECTOR ==
              offsetof(struct gdt_table, kernel_code_descriptor) &&
          GDT_KERNEL_DATA_SELECTOR ==
              offsetof(struct gdt_table, kernel_data_descriptor) &&
          GDT_TSS_SELECTOR ==
              offsetof(struct gdt_table, tss_descriptor));

    const struct gdt_tss_descriptor *desc =
        &state->gdt.tss_descriptor;

    check("TSS access byte",
          desc->access == 0x89);

    check("TSS limit equals 103",
          desc->limit_low == 103 &&
          desc->limit_high_flags == 0);

    check("TSS descriptor points to persistent TSS",
          decode_tss_base(desc) ==
          (uint64_t)(uintptr_t)&state->tss);

    check("Reserved descriptor field cleared",
          desc->reserved == 0);

    check("I/O bitmap disabled",
          state->tss.iomap_base == 104);

    check("Privilege stacks initially zero",
          state->tss.rsp[0] == 0 &&
          state->tss.rsp[1] == 0 &&
          state->tss.rsp[2] == 0);

    uint64_t df  = state->tss.ist[0];
    uint64_t nmi = state->tss.ist[1];
    uint64_t mc  = state->tss.ist[2];

    check("Three emergency IST pointers assigned",
          df != 0 && nmi != 0 && mc != 0);

    check("IST stack-top alignment",
          df % 16 == 0 &&
          nmi % 16 == 0 &&
          mc % 16 == 0);

    check("Emergency stack regions separate",
          stacks_separate(df, nmi) &&
          stacks_separate(df, mc) &&
          stacks_separate(nmi, mc));

    int unused_clear = 1;

    for (size_t i = 3; i < TSS_IST_COUNT; i++) {
        if (state->tss.ist[i] != 0) {
            unused_clear = 0;
        }
    }

    check("Unused IST pointers cleared", unused_clear);

    struct tss64 tss_copy = state->tss;
    struct gdt_table gdt_copy = state->gdt;
    struct gdt_pointer gdtr_copy = state->gdtr;

    check("Second preparation succeeds",
          bootstrap_cpu_tables_prepare() == 1);

    check("Persistent address unchanged",
          bootstrap_cpu_tables_get() == state);

    check("Prepared structures remain consistent",
          memcmp(&state->tss, &tss_copy, sizeof(tss_copy)) == 0 &&
          memcmp(&state->gdt, &gdt_copy, sizeof(gdt_copy)) == 0 &&
          memcmp(&state->gdtr, &gdtr_copy, sizeof(gdtr_copy)) == 0);

    check("Tables remain available",
          bootstrap_cpu_tables_get() != NULL);

    printf("---------------------------------\n");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
