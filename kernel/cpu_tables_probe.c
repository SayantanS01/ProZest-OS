#include <stdint.h>
#include <stddef.h>

#include "cpu_tables_probe.h"
#include "bootstrap_tables.h"
#include "bootstrap_idt.h"
#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "tss.h"

static void probe_result(const char *name, int passed)
{
    console_write(passed ? "[PASS] " : "[FAIL] ");
    console_write(name);
    console_newline();
}

int cpu_tables_probe_run(void)
{
    int success = 1;

    console_newline();
    console_write("M09 CPU TABLE PREPARATION");
    console_newline();

    int gdt_ok = bootstrap_cpu_tables_prepare();
    int idt_ok = bootstrap_idt_prepare();

    probe_result("GDT/TSS prepared", gdt_ok);
    probe_result("IDT prepared", idt_ok);

    if (!gdt_ok || !idt_ok) {
        return 0;
    }

    const struct bootstrap_cpu_tables *cpu =
        bootstrap_cpu_tables_get();

    const struct bootstrap_idt_state *irq =
        bootstrap_idt_get();

    int pointers_ok = cpu != NULL && irq != NULL;

    probe_result("Persistent pointers valid", pointers_ok);

    if (!pointers_ok) {
        return 0;
    }

    int gdtr_ok =
        cpu->gdtr.base == (uint64_t)(uintptr_t)&cpu->gdt &&
        cpu->gdtr.limit == sizeof(cpu->gdt) - 1u;

    int idtr_ok =
        irq->idtr.base == (uint64_t)(uintptr_t)&irq->table &&
        irq->idtr.limit == sizeof(irq->table) - 1u;

    int ist_ok =
        cpu->tss.ist[0] != 0 &&
        cpu->tss.ist[1] != 0 &&
        cpu->tss.ist[2] != 0;

    unsigned int present = 0;
    int selectors_ok = 1;

    for (unsigned int i = 0; i < 32; i++) {
        const struct idt_entry *gate =
            &irq->table.entries[i];

        if (gate->type_attributes == IDT_INTERRUPT_GATE) {
            present++;
        }

        if (gate->selector != GDT_KERNEL_CODE_SELECTOR) {
            selectors_ok = 0;
        }
    }

    int gates_ok = present == 32 && selectors_ok;

    probe_result("GDTR operand valid", gdtr_ok);
    probe_result("IDTR operand valid", idtr_ok);
    probe_result("Three IST stacks available", ist_ok);
    probe_result("32 exception gates valid", gates_ok);

    success = gdtr_ok && idtr_ok && ist_ok && gates_ok;

    console_write(
        success
        ? "M09 TABLE PREPARATION: PASS"
        : "M09 TABLE PREPARATION: FAIL"
    );

    console_newline();

    return success;
}
