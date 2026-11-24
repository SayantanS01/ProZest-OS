#ifndef PROZEST_QEMU_DEBUGCON_H
#define PROZEST_QEMU_DEBUGCON_H

#include <stdint.h>

/*
 * Experimental QEMU debug console.
 *
 * I/O port: 0xE9
 *
 * Requires privileged kernel execution.
 * Do not execute from Ubuntu user space.
 */

static inline void qemu_debugcon_putc(char c)
{
    __asm__ volatile (
        "outb %0, $0xe9"
        :
        : "a" ((uint8_t)c)
        : "memory"
    );
}

static inline void qemu_debugcon_puts(
    const char *message
)
{
    if (!message) {
        return;
    }

    while (*message) {
        qemu_debugcon_putc(*message++);
    }
}

static inline void qemu_debugcon_hex64(
    uint64_t value
)
{
    static const char digits[] =
        "0123456789ABCDEF";

    qemu_debugcon_puts("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        qemu_debugcon_putc(
            digits[(value >> shift) & 0xFULL]
        );
    }
}

#endif
