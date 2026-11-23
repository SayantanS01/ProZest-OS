#include <stddef.h>

#include "cpu_state.h"

/*
 * Read the current processor descriptor state.
 *
 * SGDT stores the current GDTR.
 * SIDT stores the current IDTR.
 * STR stores the current TSS selector.
 *
 * These instructions do not install
 * or replace descriptor tables.
 */
void cpu_state_capture(
    struct cpu_state_snapshot *snapshot
)
{
    if (snapshot == NULL) {
        return;
    }

    __asm__ volatile (
        "sgdt %0"
        : "=m"(snapshot->gdtr)
        :
        : "memory"
    );

    __asm__ volatile (
        "sidt %0"
        : "=m"(snapshot->idtr)
        :
        : "memory"
    );

    __asm__ volatile (
        "movw %%cs, %0"
        : "=r"(snapshot->cs)
    );

    __asm__ volatile (
        "movw %%ss, %0"
        : "=r"(snapshot->ss)
    );

    __asm__ volatile (
        "movw %%ds, %0"
        : "=r"(snapshot->ds)
    );

    __asm__ volatile (
        "movw %%es, %0"
        : "=r"(snapshot->es)
    );

    __asm__ volatile (
        "str %0"
        : "=r"(snapshot->tr)
    );
}
