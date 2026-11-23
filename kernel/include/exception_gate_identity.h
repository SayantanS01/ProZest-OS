#ifndef PROZEST_EXCEPTION_GATE_IDENTITY_H
#define PROZEST_EXCEPTION_GATE_IDENTITY_H

#include "idt.h"

/*
 * Verify that each exception gate points
 * to the correct assembly stub.
 *
 * Checks vectors 0 through 31.
 *
 * Returns 1 if all addresses match.
 * Returns 0 for NULL or any mismatch.
 *
 * Does not modify memory or CPU registers.
 */
int exception_gate_identity_check(
    const struct idt_table *table
);

#endif
