#ifndef PROZEST_EXCEPTION_H
#define PROZEST_EXCEPTION_H

#include <stdint.h>

/*
 * "0x" + 16 hexadecimal digits + '\0'.
 */
#define EXCEPTION_HEX_BUFFER_SIZE 19

/* Return a readable exception name. */
const char *exception_name(uint64_t vector);

/*
 * Return 1 if the CPU pushes an error code
 * for this exception vector.
 * Otherwise return 0.
 */
int exception_has_error_code(uint64_t vector);

/* Format an unsigned 64-bit value as hexadecimal. */
void exception_format_hex(
    uint64_t value,
    char output[EXCEPTION_HEX_BUFFER_SIZE]
);

/*
 * Fatal CPU exception dispatcher.
 *
 * Called by assembly entry stubs.
 * The console must already be initialized.
 *
 * This function never returns.
 */
__attribute__((noreturn))
void exception_dispatch(
    uint64_t vector,
    uint64_t error_code
);

#endif
