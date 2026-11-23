#include <stddef.h>
#include <stdint.h>

#include "gdt.h"
#include "tss.h"

/*
 * Encode a 16-byte x86-64 TSS descriptor.
 *
 * The base is a 64-bit linear address.
 * The limit is measured in bytes because
 * we leave the granularity flag at zero.
 */
int gdt_encode_tss_descriptor(
    struct gdt_tss_descriptor *descriptor,
    uint64_t base,
    uint32_t limit
)
{
    if (descriptor == NULL || limit > 0xFFFFFu) {
        return 0;
    }

    descriptor->limit_low =
        (uint16_t)(limit & 0xFFFFu);

    descriptor->base_low =
        (uint16_t)(base & 0xFFFFu);

    descriptor->base_middle =
        (uint8_t)((base >> 16) & 0xFFu);

    descriptor->access = GDT_TSS_ACCESS;

    /*
     * The upper four bits hold the
     * high portion of the 20-bit limit.
     *
     * All descriptor flags remain zero,
     * including granularity (G = 0).
     */
    descriptor->limit_high_flags =
        (uint8_t)((limit >> 16) & 0x0Fu);

    descriptor->base_high =
        (uint8_t)((base >> 24) & 0xFFu);

    descriptor->base_upper =
        (uint32_t)(base >> 32);

    descriptor->reserved = 0;

    return 1;
}

/*
 * Initialize the bootstrap kernel GDT.
 *
 * The caller must keep both the GDT and TSS
 * alive while the CPU uses them.
 */
int gdt_table_init(
    struct gdt_table *table,
    const struct tss64 *tss
)
{
    if (table == NULL || tss == NULL) {
        return 0;
    }

    table->null_descriptor = 0;

    table->kernel_code_descriptor =
        GDT_KERNEL_CODE_DESCRIPTOR;

    table->kernel_data_descriptor =
        GDT_KERNEL_DATA_DESCRIPTOR;

    return gdt_encode_tss_descriptor(
        &table->tss_descriptor,
        (uint64_t)(uintptr_t)tss,
        (uint32_t)(sizeof(*tss) - 1u)
    );
}

/*
 * Prepare the 10-byte GDTR operand.
 *
 * The limit is the last valid byte offset:
 * 40 - 1 = 39.
 */
int gdt_pointer_init(
    struct gdt_pointer *pointer,
    const struct gdt_table *table
)
{
    if (pointer == NULL || table == NULL) {
        return 0;
    }

    pointer->limit =
        (uint16_t)(sizeof(*table) - 1u);

    pointer->base =
        (uint64_t)(uintptr_t)table;

    return 1;
}
