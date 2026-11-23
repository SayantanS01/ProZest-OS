#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "cpu_tables_preflight.h"
#include "gdt.h"
#include "idt.h"
#include "tss.h"

static unsigned int checks = 0;
static unsigned int failures = 0;

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
 * Host-only dispatcher.
 *
 * Required because exception_stubs.S links
 * against exception_dispatch.
 *
 * The test never executes a real stub.
 */
__attribute__((noreturn))
void exception_dispatch(
    uint64_t vector,
    uint64_t error_code
)
{
    fprintf(
        stderr,
        "Unexpected stub: %llu / %llu\n",
        (unsigned long long)vector,
        (unsigned long long)error_code
    );

    abort();
}

/*
 * Modify exactly one field, verify that
 * validation rejects it, then restore it.
 *
 * This is safe here because the tables
 * are NOT active on the host CPU.
 *
 * GNU C extension: __auto_type.
 */
#define EXPECT_REJECT(name, field, invalid_value) do { \
    __auto_type original = (field);               \
    (field) = (invalid_value);                     \
    check((name),                                \
          cpu_tables_preflight_check(cpu, irq) == 0); \
    (field) = original;                           \
} while (0)

int main(void)
{
    puts("ProZest OS - CPU Preflight Negative Tests");
    puts("========================================");

    check(
        "CPU tables initially unavailable",
        bootstrap_cpu_tables_get() == NULL
    );

    check(
        "IDT initially unavailable",
        bootstrap_idt_get() == NULL
    );

    check(
        "GDT/TSS preparation",
        bootstrap_cpu_tables_prepare() == 1
    );

    check(
        "IDT preparation",
        bootstrap_idt_prepare() == 1
    );

    /*
     * Cast away const only for host-side
     * mutation testing. These objects are
     * actually allocated as writable static
     * storage by our bootstrap modules.
     */
    struct bootstrap_cpu_tables *cpu =
        (struct bootstrap_cpu_tables *)(void *)
            bootstrap_cpu_tables_get();

    struct bootstrap_idt_state *irq =
        (struct bootstrap_idt_state *)(void *)
            bootstrap_idt_get();

    check(
        "Prepared pointers available",
        cpu != NULL && irq != NULL
    );

    if (cpu == NULL || irq == NULL) {
        return 1;
    }

    check(
        "Valid tables accepted",
        cpu_tables_preflight_check(cpu, irq) == 1
    );

    check(
        "NULL CPU tables rejected",
        cpu_tables_preflight_check(NULL, irq) == 0
    );

    check(
        "NULL IDT rejected",
        cpu_tables_preflight_check(cpu, NULL) == 0
    );

    puts("\n--- GDT and IDTR corruption ---");

    EXPECT_REJECT(
        "Incorrect GDTR limit",
        cpu->gdtr.limit, 0u
    );

    EXPECT_REJECT(
        "Incorrect GDTR base",
        cpu->gdtr.base, 0ULL
    );

    EXPECT_REJECT(
        "Incorrect IDTR limit",
        irq->idtr.limit, 0u
    );

    EXPECT_REJECT(
        "Incorrect IDTR base",
        irq->idtr.base, 0ULL
    );

    EXPECT_REJECT(
        "Null GDT descriptor corrupted",
        cpu->gdt.null_descriptor, 1ULL
    );

    EXPECT_REJECT(
        "Kernel code descriptor corrupted",
        cpu->gdt.kernel_code_descriptor, 0ULL
    );

    EXPECT_REJECT(
        "Kernel data descriptor corrupted",
        cpu->gdt.kernel_data_descriptor, 0ULL
    );

    puts("\n--- TSS descriptor corruption ---");

    EXPECT_REJECT(
        "TSS access byte corrupted",
        cpu->gdt.tss_descriptor.access, 0u
    );

    EXPECT_REJECT(
        "TSS limit corrupted",
        cpu->gdt.tss_descriptor.limit_low, 0u
    );

    EXPECT_REJECT(
        "TSS address corrupted",
        cpu->gdt.tss_descriptor.base_low,
        (uint16_t)(
            cpu->gdt.tss_descriptor.base_low ^ 1u
        )
    );

    EXPECT_REJECT(
        "TSS reserved field corrupted",
        cpu->gdt.tss_descriptor.reserved, 1u
    );

    EXPECT_REJECT(
        "TSS I/O bitmap offset corrupted",
        cpu->tss.iomap_base, 0u
    );

    puts("\n--- Emergency stack corruption ---");

    EXPECT_REJECT(
        "Double-fault stack missing",
        cpu->tss.ist[0], 0ULL
    );

    EXPECT_REJECT(
        "NMI stack misaligned",
        cpu->tss.ist[1],
        cpu->tss.ist[1] + 1u
    );

    EXPECT_REJECT(
        "Machine-check stack overlaps double-fault stack",
        cpu->tss.ist[2],
        cpu->tss.ist[0]
    );

    EXPECT_REJECT(
        "Unused IST4 unexpectedly assigned",
        cpu->tss.ist[3],
        cpu->tss.ist[0]
    );

    puts("\n--- IDT gate corruption ---");

    EXPECT_REJECT(
        "Vector 0 incorrect code selector",
        irq->table.entries[0].selector, 0x28u
    );

    EXPECT_REJECT(
        "NMI incorrect IST index",
        irq->table.entries[2].ist, 0u
    );

    EXPECT_REJECT(
        "Page-fault gate not present",
        irq->table.entries[14].type_attributes,
        0x0Eu
    );

    EXPECT_REJECT(
        "Page-fault handler noncanonical",
        irq->table.entries[14].offset_high,
        0x00010000u
    );

    EXPECT_REJECT(
        "Vector 31 reserved field corrupted",
        irq->table.entries[31].reserved,
        1u
    );

    EXPECT_REJECT(
        "Unexpected hardware IRQ gate",
        irq->table.entries[32].type_attributes,
        IDT_INTERRUPT_GATE
    );

    /*
     * All mutations must now have been
     * undone.
     */
    puts("\n--- Final restoration check ---");

    check(
        "Original valid tables accepted again",
        cpu_tables_preflight_check(cpu, irq) == 1
    );

    puts("----------------------------------------");

    printf("Checks: %u\n", checks);
    printf("Failures: %u\n", failures);

    return failures == 0 ? 0 : 1;
}
