#include <stddef.h>
#include <stdint.h>

#include "idt.h"

/*
 * Encode a 64-bit handler address into
 * the three address fields of an IDT gate.
 *
 * This function only prepares memory.
 * It does not execute LIDT.
 */
void idt_encode_gate(
    struct idt_entry *entry,
    uint64_t handler_address,
    uint16_t selector,
    uint8_t ist,
    uint8_t attributes
)
{
    if (entry == NULL) {
        return;
    }

    entry->offset_low =
        (uint16_t)(handler_address & 0xFFFFu);

    entry->selector = selector;

    /*
     * Only the lowest three bits select
     * an Interrupt Stack Table entry.
     *
     * Zero means do not switch via IST.
     */
    entry->ist = (uint8_t)(ist & 0x07u);

    entry->type_attributes = attributes;

    entry->offset_middle =
        (uint16_t)((handler_address >> 16) & 0xFFFFu);

    entry->offset_high =
        (uint32_t)(handler_address >> 32);

    entry->reserved = 0;
}

/*
 * Clear the complete IDT.
 *
 * We do not depend on memset() because
 * ProZest OS has no C standard library.
 */
void idt_table_init(struct idt_table *table)
{
    if (table == NULL) {
        return;
    }

    for (uint16_t i = 0; i < IDT_MAX_ENTRIES; i++) {
        struct idt_entry *entry = &table->entries[i];

        entry->offset_low = 0;
        entry->selector = 0;
        entry->ist = 0;
        entry->type_attributes = 0;
        entry->offset_middle = 0;
        entry->offset_high = 0;
        entry->reserved = 0;
    }
}

/*
 * Install a gate descriptor into a vector slot.
 *
 * This changes only table memory.
 * It does NOT execute LIDT.
 */
int idt_table_set_gate(
    struct idt_table *table,
    uint16_t vector,
    uint64_t handler_address,
    uint16_t selector,
    uint8_t ist,
    uint8_t attributes
)
{
    if (table == NULL || vector >= IDT_MAX_ENTRIES) {
        return 0;
    }

    idt_encode_gate(
        &table->entries[vector],
        handler_address,
        selector,
        ist,
        attributes
    );

    return 1;
}
