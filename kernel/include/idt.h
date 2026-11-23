#ifndef PROZEST_IDT_H
#define PROZEST_IDT_H

#include <stdint.h>

/*
 * x86-64 Interrupt Descriptor Table.
 *
 * The CPU supports up to 256 vectors.
 */
#define IDT_MAX_ENTRIES 256

/*
 * Present, ring-0, 64-bit interrupt gate.
 */
#define IDT_INTERRUPT_GATE 0x8Eu

/*
 * A 64-bit IDT gate occupies exactly 16 bytes.
 *
 * This structure follows the x86-64
 * interrupt/trap gate descriptor layout.
 */
struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

/*
 * The IDTR register is loaded using LIDT.
 *
 * limit = size of table in bytes - 1
 * base  = linear address of the table
 */
struct __attribute__((packed)) idt_pointer {
    uint16_t limit;
    uint64_t base;
};

/* Required binary layout checks. */
_Static_assert(
    sizeof(struct idt_entry) == 16,
    "IDT entry must be exactly 16 bytes"
);

_Static_assert(
    sizeof(struct idt_pointer) == 10,
    "IDTR pointer must be exactly 10 bytes"
);

/*
 * Construct one 64-bit IDT gate.
 *
 * This only writes a descriptor in memory.
 * It does NOT install the IDT or enable interrupts.
 */
void idt_encode_gate(
    struct idt_entry *entry,
    uint64_t handler_address,
    uint16_t selector,
    uint8_t ist,
    uint8_t attributes
);


/*
 * Complete 256-entry IDT.
 *
 * Each gate starts in the not-present state
 * until a handler is explicitly installed.
 */
struct idt_table {
    struct idt_entry entries[IDT_MAX_ENTRIES];
};

_Static_assert(
    sizeof(struct idt_table) == 4096,
    "Complete IDT must occupy 4096 bytes"
);

/* Clear every entry to a not-present state. */
void idt_table_init(struct idt_table *table);

/*
 * Install an encoded gate at a chosen vector.
 *
 * Returns 1 on success, 0 for invalid input.
 */
int idt_table_set_gate(
    struct idt_table *table,
    uint16_t vector,
    uint64_t handler_address,
    uint16_t selector,
    uint8_t ist,
    uint8_t attributes
);

#endif
