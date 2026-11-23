#include <stddef.h>
#include <stdint.h>

#include "tss.h"

/*
 * Dedicated exception stacks for the bootstrap CPU.
 *
 * These are static storage, not heap allocations.
 * They must remain accessible for the lifetime
 * of the active TSS.
 */

static uint8_t double_fault_stack[TSS_IST_STACK_SIZE]
    __attribute__((aligned(16)));

static uint8_t nmi_stack[TSS_IST_STACK_SIZE]
    __attribute__((aligned(16)));

static uint8_t machine_check_stack[TSS_IST_STACK_SIZE]
    __attribute__((aligned(16)));

_Static_assert(
    (TSS_IST_STACK_SIZE % 16u) == 0,
    "IST stack size must be 16-byte aligned"
);

/*
 * Prepare the TSS for the bootstrap processor.
 *
 * This writes memory only.
 * The CPU does not use this TSS until it is
 * installed through a valid GDT and LTR.
 */
void tss_prepare_bootstrap(struct tss64 *tss)
{
    if (tss == NULL) {
        return;
    }

    tss->reserved0 = 0;
    tss->reserved1 = 0;
    tss->reserved2 = 0;
    tss->reserved3 = 0;

    for (uint32_t i = 0; i < 3; i++) {
        tss->rsp[i] = 0;
    }

    for (uint32_t i = 0; i < TSS_IST_COUNT; i++) {
        tss->ist[i] = 0;
    }

    /*
     * IST1: double fault.
     * The initial stack pointer is the
     * address just beyond the array.
     */
    tss->ist[TSS_IST_DOUBLE_FAULT - 1] =
        (uint64_t)(uintptr_t)(
            double_fault_stack + sizeof(double_fault_stack)
        );

    /* IST2: non-maskable interrupt. */
    tss->ist[TSS_IST_NMI - 1] =
        (uint64_t)(uintptr_t)(
            nmi_stack + sizeof(nmi_stack)
        );

    /* IST3: machine-check exception. */
    tss->ist[TSS_IST_MACHINE_CHECK - 1] =
        (uint64_t)(uintptr_t)(
            machine_check_stack + sizeof(machine_check_stack)
        );

    /*
     * No I/O permission bitmap is supplied.
     * The future TSS descriptor limit will
     * be sizeof(struct tss64) - 1.
     */
    tss->iomap_base = (uint16_t)sizeof(*tss);
}
