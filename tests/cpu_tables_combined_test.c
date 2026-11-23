#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "bootstrap_tables.h"
#include "bootstrap_idt.h"
#include "idt.h"
#include "gdt.h"
#include "tss.h"

/*
 * The real exception stubs require this symbol
 * for linking. Host tests must not call them.
 */
__attribute__((noreturn))
void exception_dispatch(uint64_t vector, uint64_t error_code)
{
    fprintf(stderr,
            "Unexpected exception: %llu, %llu\n",
            (unsigned long long)vector,
            (unsigned long long)error_code);
    abort();
}

static unsigned int checks = 0;
static unsigned int failures = 0;

static void check(const char *name, int condition)
{
    checks++;

    printf("[%s] %s\n",
           condition ? "PASS" : "FAIL",
           name);

    if (!condition) {
        failures++;
    }
}

static uint8_t expected_ist(unsigned int vector)
{
    if (vector == 2)  return TSS_IST_NMI;
    if (vector == 8)  return TSS_IST_DOUBLE_FAULT;
    if (vector == 18) return TSS_IST_MACHINE_CHECK;

    return 0;
}

static uint64_t decode_tss_base(
    const struct gdt_tss_descriptor *d)
{
    return (uint64_t)d->base_low |
           ((uint64_t)d->base_middle << 16) |
           ((uint64_t)d->base_high << 24) |
           ((uint64_t)d->base_upper << 32);
}

int main(void)
{
    puts("ProZest OS - Combined CPU Tables");
    puts("================================");

    check("GDT/TSS initially unavailable",
          bootstrap_cpu_tables_get() == NULL);

    check("IDT initially unavailable",
          bootstrap_idt_get() == NULL);

    check("GDT/TSS preparation succeeds",
          bootstrap_cpu_tables_prepare() == 1);

    check("IDT preparation succeeds",
          bootstrap_idt_prepare() == 1);

    const struct bootstrap_cpu_tables *cpu =
        bootstrap_cpu_tables_get();

    const struct bootstrap_idt_state *interrupts =
        bootstrap_idt_get();

    check("CPU tables accessible", cpu != NULL);
    check("IDT accessible", interrupts != NULL);

    if (cpu == NULL || interrupts == NULL) {
        return 1;
    }

    check("GDT address stable",
          cpu == bootstrap_cpu_tables_get());

    check("IDT address stable",
          interrupts == bootstrap_idt_get());

    check("GDTR points to our GDT",
          cpu->gdtr.base ==
          (uint64_t)(uintptr_t)&cpu->gdt);

    check("IDTR points to our IDT",
          interrupts->idtr.base ==
          (uint64_t)(uintptr_t)&interrupts->table);

    check("GDTR limit correct",
          cpu->gdtr.limit == sizeof(cpu->gdt) - 1u);

    check("IDTR limit correct",
          interrupts->idtr.limit ==
          sizeof(interrupts->table) - 1u);

    check("Kernel code selector is 0x08",
          GDT_KERNEL_CODE_SELECTOR == 0x08u);

    check("Kernel data selector is 0x10",
          GDT_KERNEL_DATA_SELECTOR == 0x10u);

    check("TSS selector is 0x18",
          GDT_TSS_SELECTOR == 0x18u);

    check("Code descriptor matches selector",
          cpu->gdt.kernel_code_descriptor ==
          GDT_KERNEL_CODE_DESCRIPTOR);

    check("Data descriptor matches selector",
          cpu->gdt.kernel_data_descriptor ==
          GDT_KERNEL_DATA_DESCRIPTOR);

    check("TSS descriptor points to our TSS",
          decode_tss_base(&cpu->gdt.tss_descriptor) ==
          (uint64_t)(uintptr_t)&cpu->tss);

    check("TSS descriptor present",
          cpu->gdt.tss_descriptor.access == 0x89u);

    check("All three emergency stacks exist",
          cpu->tss.ist[0] != 0 &&
          cpu->tss.ist[1] != 0 &&
          cpu->tss.ist[2] != 0);

    check("Emergency stack tops aligned",
          cpu->tss.ist[0] % 16u == 0 &&
          cpu->tss.ist[1] % 16u == 0 &&
          cpu->tss.ist[2] % 16u == 0);

    unsigned int present = 0;
    unsigned int emergency_gates = 0;
    int selector_match = 1;
    int ist_match = 1;
    int attributes_match = 1;

    for (unsigned int vector = 0; vector < 32; vector++) {
        const struct idt_entry *gate =
            &interrupts->table.entries[vector];

        if (gate->selector != GDT_KERNEL_CODE_SELECTOR) {
            selector_match = 0;
        }

        if (gate->ist != expected_ist(vector)) {
            ist_match = 0;
        }

        if (gate->type_attributes != IDT_INTERRUPT_GATE ||
            gate->reserved != 0) {
            attributes_match = 0;
        }

        if (gate->type_attributes & 0x80u) {
            present++;
        }

        if (gate->ist != 0) {
            emergency_gates++;
        }
    }

    check("All IDT code selectors match GDT",
          selector_match);

    check("All IST indices match TSS design",
          ist_match);

    check("All exception gate attributes valid",
          attributes_match);

    check("Exactly 32 exception gates present",
          present == 32);

    check("Exactly three IST gates",
          emergency_gates == 3);

    check("RSP0 deliberately not configured yet",
          cpu->tss.rsp[0] == 0);

    int other_vectors_absent = 1;

    for (unsigned int vector = 32;
         vector < IDT_MAX_ENTRIES;
         vector++) {

        if (interrupts->table.entries[vector]
                .type_attributes & 0x80u) {
            other_vectors_absent = 0;
        }
    }

    check("Hardware IRQ vectors remain absent",
          other_vectors_absent);

    check("CPU and IDT storage do not overlap",
          (uintptr_t)cpu + sizeof(*cpu) <=
              (uintptr_t)interrupts ||
          (uintptr_t)interrupts +
              sizeof(*interrupts) <= (uintptr_t)cpu);

    puts("--------------------------------");
    printf("Checks: %u\n", checks);
    printf("Failures: %u\n", failures);

    return failures == 0 ? 0 : 1;
}
