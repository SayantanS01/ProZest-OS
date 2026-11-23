#ifndef PROZEST_GDT_H
#define PROZEST_GDT_H

#include <stdint.h>
#include <stddef.h>

/*
 * ProZest OS x86-64 Global Descriptor Table.
 *
 * Selector values are byte offsets from
 * the beginning of the GDT.
 */
#define GDT_KERNEL_CODE_SELECTOR 0x08u
#define GDT_KERNEL_DATA_SELECTOR 0x10u
#define GDT_TSS_SELECTOR         0x18u

/*
 * Available, present, ring-0 64-bit TSS.
 *
 * P=1, DPL=0, S=0, Type=1001.
 */
#define GDT_TSS_ACCESS 0x89u

/*
 * 64-bit TSS system descriptor.
 *
 * Occupies two consecutive GDT slots.
 */
struct __attribute__((packed)) gdt_tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;

    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;

    uint32_t base_upper;
    uint32_t reserved;
};

/*
 * Initial bootstrap CPU GDT:
 *
 * Slot 0: null
 * Slot 1: kernel code
 * Slot 2: kernel data
 * Slots 3-4: 64-bit TSS descriptor
 */
struct __attribute__((packed, aligned(8))) gdt_table {
    uint64_t null_descriptor;
    uint64_t kernel_code_descriptor;
    uint64_t kernel_data_descriptor;

    struct gdt_tss_descriptor tss_descriptor;
};

/*
 * 10-byte GDTR operand for LGDT
 * in x86-64 mode.
 */
struct __attribute__((packed)) gdt_pointer {
    uint16_t limit;
    uint64_t base;
};

/* Hardware layout checks. */

_Static_assert(
    sizeof(struct gdt_tss_descriptor) == 16,
    "TSS descriptor must be 16 bytes"
);

_Static_assert(
    sizeof(struct gdt_table) == 40,
    "Bootstrap GDT must be 40 bytes"
);

_Static_assert(
    sizeof(struct gdt_pointer) == 10,
    "GDTR operand must be 10 bytes"
);

_Static_assert(
    offsetof(struct gdt_table, tss_descriptor) == 24,
    "TSS selector must point to byte 24"
);


/*
 * Encode a 64-bit available TSS descriptor.
 *
 * Returns 1 on success.
 * Returns 0 for NULL or a limit exceeding
 * the 20-bit descriptor limit field.
 *
 * This only writes memory. It does not
 * execute LGDT or LTR.
 */
int gdt_encode_tss_descriptor(
    struct gdt_tss_descriptor *descriptor,
    uint64_t base,
    uint32_t limit
);


/*
 * Bootstrap ring-0 code and data descriptors.
 *
 * 64-bit code:
 *   Present, ring 0, executable, readable,
 *   L=1, D=0.
 */
#define GDT_KERNEL_CODE_DESCRIPTOR 0x00AF9A000000FFFFULL

/*
 * Ring-0 writable data descriptor.
 */
#define GDT_KERNEL_DATA_DESCRIPTOR 0x00CF92000000FFFFULL

/* Forward declaration of the TSS structure. */
struct tss64;

/*
 * Build the complete GDT in memory.
 *
 * Returns 1 on success, 0 for NULL input.
 */
int gdt_table_init(
    struct gdt_table *table,
    const struct tss64 *tss
);

/*
 * Build the GDTR operand for this GDT.
 *
 * Does not execute LGDT.
 */
int gdt_pointer_init(
    struct gdt_pointer *pointer,
    const struct gdt_table *table
);

#endif
