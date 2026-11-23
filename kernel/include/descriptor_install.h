#ifndef PROZEST_DESCRIPTOR_INSTALL_H
#define PROZEST_DESCRIPTOR_INSTALL_H

#include "gdt.h"
#include "idt.h"

/*
 * EXPERIMENTAL - NOT READY FOR EXECUTION.
 *
 * Install GDT, reload segment selectors,
 * load TSS and install IDT.
 *
 * This operation modifies CPU registers.
 * It must only run in ring 0 after a
 * complete activation preflight.
 *
 * The function leaves interrupts disabled.
 *
 * Do not call from kernel_main yet.
 */
void cpu_descriptors_install(
    const struct gdt_pointer *gdtr,
    const struct idt_pointer *idtr
);

#endif
