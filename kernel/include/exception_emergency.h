#ifndef PROZEST_EXCEPTION_EMERGENCY_H
#define PROZEST_EXCEPTION_EMERGENCY_H

#include <stdint.h>

/*
 * Emergency exception reporting for QEMU.
 *
 * First entry:
 *   Report vector and error code.
 *
 * Repeated entry:
 *   Report nested exception and halt.
 *
 * Requires privileged kernel execution.
 */
void exception_emergency_report(
    uint64_t vector,
    uint64_t error_code
);

#endif
