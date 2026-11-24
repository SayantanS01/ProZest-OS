#include <stdint.h>

#include "exception_emergency.h"
#include "exception.h"
#include "qemu_debugcon.h"

/*
 * Zero = no fatal exception being handled.
 * One  = fatal exception handling started.
 *
 * Never reset during fatal exception handling.
 */
static unsigned int panic_active = 0;

_Static_assert(
    __atomic_always_lock_free(
        sizeof(unsigned int), 0
    ),
    "Panic guard must be lock-free"
);

static __attribute__((noreturn))
void emergency_halt(void)
{
    for (;;) {
        __asm__ volatile (
            "cli; hlt" ::: "memory"
        );
    }
}

void exception_emergency_report(
    uint64_t vector,
    uint64_t error_code
)
{
    /*
     * Atomically mark fatal exception handling
     * as active and retrieve its previous state.
     */
    unsigned int previous =
        __atomic_exchange_n(
            &panic_active,
            1u,
            __ATOMIC_RELAXED
        );

    if (previous != 0u) {
        qemu_debugcon_puts(
            "\nNESTED CPU EXCEPTION\n"
        );

        qemu_debugcon_puts("Vector: ");
        qemu_debugcon_hex64(vector);
        qemu_debugcon_putc('\n');

        qemu_debugcon_puts(
            "EMERGENCY HALT\n"
        );

        emergency_halt();
    }

    /*
     * Independent diagnostic output.
     * No graphical console or normal logger.
     */
    qemu_debugcon_puts(
        "\nFATAL CPU EXCEPTION\n"
    );

    qemu_debugcon_puts("Vector: ");
    qemu_debugcon_hex64(vector);
    qemu_debugcon_putc('\n');

    qemu_debugcon_puts("Exception: ");
    qemu_debugcon_puts(exception_name(vector));
    qemu_debugcon_putc('\n');

    if (exception_has_error_code(vector)) {
        qemu_debugcon_puts("CPU error code: ");
        qemu_debugcon_hex64(error_code);
        qemu_debugcon_putc('\n');
    } else {
        qemu_debugcon_puts(
            "CPU error code: not provided\n"
        );
    }

    qemu_debugcon_puts(
        "EMERGENCY REPORT COMPLETE\n"
    );
}

#ifdef PROZEST_UNIFIED_FATAL
#include "fatal_frame.h"

static void diagnostic(const char *label, uint64_t value)
{
    qemu_debugcon_puts(label);
    qemu_debugcon_hex64(value);
    qemu_debugcon_putc('\n');
}

_Noreturn void hardware_fatal(uint64_t vector, const uint64_t *raw, uint64_t cr2)
{
    if (__atomic_exchange_n(&panic_active, 1u, __ATOMIC_RELAXED)) {
        qemu_debugcon_puts("NESTED CPU EXCEPTION\n");
        emergency_halt();
    }
    qemu_debugcon_puts("FATAL CPU EXCEPTION\n");
    diagnostic("Vector: ", vector);
    diagnostic("Handler entry RSP: ", (uintptr_t)raw);
    const struct fatal_frame *f = fatal_frame_at(vector, raw);
    if (!f) {
        qemu_debugcon_puts("Invalid frame\n");
        emergency_halt();
    }
    qemu_debugcon_puts(exception_name(vector));
    qemu_debugcon_putc('\n');
    if (exception_has_error_code(vector))
        diagnostic("CPU error code: ", raw[0]);
    else
        qemu_debugcon_puts("CPU error code: not provided\n");
    if (vector == 14)
        diagnostic("CR2: ", cr2);
    diagnostic("Saved RIP: ", f->rip);
    diagnostic("Saved CS: ", f->cs);
    diagnostic("Saved RFLAGS: ", f->flags);
    diagnostic("Saved RSP: ", f->rsp);
    diagnostic("Saved SS: ", f->ss);
    qemu_debugcon_puts("EMERGENCY REPORT COMPLETE\n");
    emergency_halt();
}
#endif
