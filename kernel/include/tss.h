#ifndef PROZEST_TSS_H
#define PROZEST_TSS_H

#include <stdint.h>
#include <stddef.h>

/*
 * x86-64 Task State Segment.
 *
 * The CPU requires this exact binary layout.
 *
 * This is only a structure definition.
 * No TSS is installed into the CPU yet.
 */

#define TSS_IST_COUNT 7

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;

    /* Stack pointers for privilege transitions. */
    uint64_t rsp[3];

    uint64_t reserved1;

    /*
     * Interrupt Stack Table pointers.
     *
     * ist[0] corresponds to hardware IST1.
     * ist[6] corresponds to hardware IST7.
     */
    uint64_t ist[TSS_IST_COUNT];

    uint64_t reserved2;
    uint16_t reserved3;

    /* Offset of optional I/O permission bitmap. */
    uint16_t iomap_base;
};

/* Verify the hardware-required structure layout. */

_Static_assert(
    sizeof(struct tss64) == 104,
    "x86-64 TSS must occupy 104 bytes"
);

_Static_assert(
    offsetof(struct tss64, rsp) == 4,
    "RSP0 must start at byte 4"
);

_Static_assert(
    offsetof(struct tss64, ist) == 36,
    "IST1 must start at byte 36"
);

_Static_assert(
    offsetof(struct tss64, iomap_base) == 102,
    "I/O map base must be at byte 102"
);


/*
 * Bootstrap CPU emergency exception stacks.
 *
 * IST indices are one-based in IDT descriptors.
 */
#define TSS_IST_STACK_SIZE (16u * 1024u)

#define TSS_IST_DOUBLE_FAULT   1u
#define TSS_IST_NMI            2u
#define TSS_IST_MACHINE_CHECK  3u

/*
 * Prepare a TSS and configure three IST pointers.
 *
 * This function does not execute LTR,
 * install a GDT, or activate the TSS.
 */
void tss_prepare_bootstrap(struct tss64 *tss);

#endif
