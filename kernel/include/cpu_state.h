#ifndef PROZEST_CPU_STATE_H
#define PROZEST_CPU_STATE_H

#include <stdint.h>
#include <stddef.h>

#include "gdt.h"
#include "idt.h"

/*
 * Read-only snapshot of the current
 * x86-64 CPU descriptor-table state.
 */
struct cpu_state_snapshot {
    struct gdt_pointer gdtr;
    struct idt_pointer idtr;

    uint16_t cs;
    uint16_t ss;
    uint16_t ds;
    uint16_t es;
    uint16_t tr;
};

_Static_assert(
    sizeof(struct cpu_state_snapshot) == 30,
    "CPU snapshot must be 30 bytes"
);

_Static_assert(
    offsetof(struct cpu_state_snapshot, idtr) == 10,
    "IDTR must start at byte 10"
);

_Static_assert(
    offsetof(struct cpu_state_snapshot, tr) == 28,
    "TR must start at byte 28"
);

/*
 * Capture descriptor-table registers
 * and segment selectors.
 *
 * Must be called from the kernel.
 * No processor state is modified.
 */
void cpu_state_capture(
    struct cpu_state_snapshot *snapshot
);

#endif
