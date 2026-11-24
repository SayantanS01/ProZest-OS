#include <stddef.h>
#include <stdint.h>

#include "exception.h"
#include "console.h"
#include "logger.h"

#ifdef PROZEST_QEMU_DEBUGCON
#include "exception_emergency.h"
#endif


/*
 * Convert supported CPU exception vectors
 * to human-readable names.
 */
/*
 * Exception names for architectural vectors 0-31.
 *
 * Some vectors are reserved or vendor-specific.
 */
static const char *const exception_names[32] = {
    "Division error (#DE)",
    "Debug exception (#DB)",
    "Non-maskable interrupt (NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND range exceeded (#BR)",
    "Invalid opcode (#UD)",
    "Device not available (#NM)",
    "Double fault (#DF)",
    "Reserved CPU exception vector",
    "Invalid TSS (#TS)",
    "Segment not present (#NP)",
    "Stack-segment fault (#SS)",
    "General protection (#GP)",
    "Page fault (#PF)",
    "Reserved CPU exception vector",
    "x87 floating-point (#MF)",
    "Alignment check (#AC)",
    "Machine check (#MC)",
    "SIMD floating-point (#XM)",
    "Virtualization exception (#VE)",
    "Control protection (#CP)",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Reserved CPU exception vector",
    "Hypervisor injection (#HV)",
    "VMM communication (#VC)",
    "Security exception (#SX)",
    "Reserved CPU exception vector"
};

const char *exception_name(uint64_t vector)
{
    if (vector >= 32) {
        return "Unknown CPU exception";
    }

    return exception_names[vector];
}

/*
 * The CPU pushes an exception error code
 * for these vectors.
 *
 * #CP, #VC and #SX are feature- or
 * vendor-dependent exceptions.
 */
int exception_has_error_code(uint64_t vector)
{
    switch (vector) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
        case 21:
        case 29:
        case 30:
            return 1;

        default:
            return 0;
    }
}

/*
 * Format a full 64-bit hexadecimal value.
 *
 * Example:
 *   0x000000000000000E
 *
 * No libc functions are required.
 */
void exception_format_hex(
    uint64_t value,
    char output[EXCEPTION_HEX_BUFFER_SIZE]
)
{
    if (output == NULL) {
        return;
    }

    static const char digits[] =
        "0123456789ABCDEF";

    output[0] = '0';
    output[1] = 'x';

    for (uint32_t i = 0; i < 16; i++) {
        uint32_t shift = (15u - i) * 4u;

        uint32_t digit =
            (uint32_t)((value >> shift) & 0xFu);

        output[i + 2] = digits[digit];
    }

    output[18] = '\0';
}

/*
 * Fatal CPU exception handler.
 *
 * We do not return to the faulting instruction.
 *
 * The IDT must only be activated after
 * the logger and console are initialized.
 */
__attribute__((noreturn))
void exception_dispatch(
    uint64_t vector,
    uint64_t error_code
)
{
#ifdef PROZEST_QEMU_DEBUGCON
    /*
     * Report the exception before touching
     * the graphical console.
     */
    exception_emergency_report(
        vector,
        error_code
    );
#endif

    char hex[EXCEPTION_HEX_BUFFER_SIZE];

    console_newline();

    log_write(
        LOG_ERROR,
        "FATAL CPU EXCEPTION"
    );

    log_write(
        LOG_ERROR,
        exception_name(vector)
    );

    exception_format_hex(vector, hex);

    console_write("Vector: ");
    console_write(hex);
    console_newline();

    if (exception_has_error_code(vector)) {
        exception_format_hex(error_code, hex);

        console_write("CPU error code: ");
        console_write(hex);
        console_newline();
    } else {
        console_write(
            "CPU error code: not provided\n"
        );
    }

    console_newline();

    log_write(
        LOG_ERROR,
        "Kernel execution stopped"
    );

    /*
     * Disable maskable interrupts and halt.
     *
     * HLT may wake for certain events, so
     * repeat it indefinitely.
     */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
